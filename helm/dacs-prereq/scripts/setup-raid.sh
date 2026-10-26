#!/bin/bash
# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Prepares local NVMe data disks for use as a DACS cache directory.
# Uses container tools with host devices and explicit host mount operations.

set -euo pipefail

readonly RAID_NAME="dacs"
readonly FS_LABEL="dacs"
# Keep image-created filesystems compatible with Ubuntu 22.04's e2fsck.
readonly EXT4_FEATURES="^orphan_file,^metadata_csum_seed"
readonly HOST_ROOT="/proc/1/root"
MOUNT_POINT="${MOUNT_POINT:-/var/lib/ssd/cacheserver}"
readonly NODE_HOSTNAME="${NODE_NAME:-$(uname -n 2>/dev/null || echo '<unknown>')}"

log() {
  echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] $*"
}

dump_diag() {
  log "----- DEBUG (begin) -----"
  echo "hostname=${NODE_HOSTNAME}"
  lsblk -o NAME,TYPE,SIZE,FSTYPE,LABEL,MOUNTPOINT 2>/dev/null || true
  echo ""
  cat /proc/mdstat 2>/dev/null || true
  echo ""
  mdadm --detail --scan 2>/dev/null || true
  log "----- DEBUG (end) -----"
}

persist_config() {
  # Write mdadm.conf and fstab entries for reboot persistence.
  mkdir -p "${HOST_ROOT}/etc/mdadm"
  mdadm --detail --scan 2>/dev/null | tee "${HOST_ROOT}/etc/mdadm/mdadm.conf" >/dev/null || true
  if grep -q "^LABEL=${FS_LABEL}[[:space:]]" "${HOST_ROOT}/etc/fstab" 2>/dev/null; then
    # Ensure existing entry includes noatime (older entries may not).
    if ! grep "^LABEL=${FS_LABEL}[[:space:]]" "${HOST_ROOT}/etc/fstab" | grep -q "noatime"; then
      sed -i "/^LABEL=${FS_LABEL}[[:space:]]/s/defaults/defaults,noatime/" "${HOST_ROOT}/etc/fstab"
    fi
  else
    echo "LABEL=${FS_LABEL} ${MOUNT_POINT} ext4 defaults,noatime,nofail 0 2" >> "${HOST_ROOT}/etc/fstab"
  fi
  nsenter -t 1 -m --root --wdns=/ -- /bin/sh -c \
    'if command -v update-initramfs >/dev/null 2>&1; then update-initramfs -u; fi'
}

log "========== NVMe setup starting on ${NODE_HOSTNAME} =========="

# --- Fast path: already mounted ------------------------------------------------
if findmnt --task 1 --mountpoint "${MOUNT_POINT}" -n &>/dev/null; then
  log "${MOUNT_POINT} already mounted. Nothing to do."
  exit 0
fi

# --- Require tools from the cache-setup image ---------------------------------
for tool in mdadm mkfs.ext4 blkid findmnt mount; do
  if ! command -v "$tool" &>/dev/null; then
    log "ERROR: Required tool ${tool} is missing from the cache-setup image."
    exit 1
  fi
done

mkdir -p "${HOST_ROOT}${MOUNT_POINT}"

# --- Try to reassemble existing RAID arrays ------------------------------------
MDADM_CONFIG=()
for config in /etc/mdadm.conf /etc/mdadm/mdadm.conf; do
  if [[ -f "${HOST_ROOT}${config}" ]]; then
    MDADM_CONFIG=(--config="${HOST_ROOT}${config}")
    break
  fi
done
mdadm --assemble --scan "${MDADM_CONFIG[@]}" 2>/dev/null || true

# --- Check for existing filesystem with our label (covers RAID and single-disk)
LABELED_DEV=$(blkid -L "${FS_LABEL}" 2>/dev/null || true)
if [[ -n "${LABELED_DEV}" ]]; then
  log "Found existing filesystem labeled '${FS_LABEL}' on ${LABELED_DEV}"
  mount --namespace 1 -o noatime "${LABELED_DEV}" "${MOUNT_POINT}"
  persist_config
  log "Mounted ${LABELED_DEV}. Done."
  exit 0
fi

# --- Find unused NVMe devices -------------------------------------------------
ALL_NVME_DEVICES=$(ls /dev/nvme*n* 2>/dev/null || true)
UNUSED_DEVICES=()

for device in ${ALL_NVME_DEVICES}; do
  # Skip partitions (only want whole disks)
  [[ "$device" =~ p[0-9]+$ ]] && continue
  # Skip RAID members
  if mdadm --examine "$device" 2>/dev/null | grep -q 'RAID superblock'; then
    continue
  fi
  # Skip mounted devices (catches root/OS disk)
  if findmnt --task 1 -S "$device" -n &>/dev/null; then
    continue
  fi
  # Skip devices with existing signatures
  if blkid "$device" &>/dev/null; then
    continue
  fi
  UNUSED_DEVICES+=("$device")
done

if [[ ${#UNUSED_DEVICES[@]} -eq 0 ]]; then
  log "No unused NVMe devices found."
  exit 0
fi

log "Found ${#UNUSED_DEVICES[@]} unused NVMe device(s): ${UNUSED_DEVICES[*]}"

# --- Single device: format and mount directly (no RAID) ------------------------
if [[ ${#UNUSED_DEVICES[@]} -eq 1 ]]; then
  DEVICE="${UNUSED_DEVICES[0]}"
  log "Formatting ${DEVICE} as ext4 (label '${FS_LABEL}')"
  if ! mkfs.ext4 -F -O "${EXT4_FEATURES}" -L "${FS_LABEL}" "${DEVICE}"; then
    log "ERROR: mkfs failed on ${DEVICE}."
    dump_diag
    exit 1
  fi
  mount --namespace 1 -o noatime "${DEVICE}" "${MOUNT_POINT}"
  persist_config
  log "Single-disk setup complete."
  exit 0
fi

# --- Multiple devices: create RAID0 -------------------------------------------
log "Creating RAID0 with ${#UNUSED_DEVICES[@]} devices:"
for device in "${UNUSED_DEVICES[@]}"; do
  log "  - $device"
done

RAID_DEVICE="/dev/md0"
if ! mdadm --create "${RAID_DEVICE}" \
  --name="${RAID_NAME}" \
  --level=0 \
  --raid-devices="${#UNUSED_DEVICES[@]}" \
  "${UNUSED_DEVICES[@]}" \
  --run \
  --force; then
  log "ERROR: Failed to create RAID array."
  dump_diag
  exit 1
fi

log "Waiting for RAID device..."
sleep 5

log "Formatting ${RAID_DEVICE} as ext4 (label '${FS_LABEL}')"
if ! mkfs.ext4 -F -O "${EXT4_FEATURES}" -L "${FS_LABEL}" "${RAID_DEVICE}"; then
  log "ERROR: mkfs failed."
  dump_diag
  exit 1
fi

mount --namespace 1 -o noatime "${RAID_DEVICE}" "${MOUNT_POINT}"
chmod 0777 "${HOST_ROOT}${MOUNT_POINT}"
persist_config

log "RAID0 setup complete."
