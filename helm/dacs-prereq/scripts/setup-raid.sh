#!/bin/bash
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.
#
# Prepares local NVMe data disks for use as a DACS cache directory.
# Executed via nsenter in the host namespace (see daemonset.yaml).

set -euo pipefail

readonly RAID_NAME="dacs"
readonly FS_LABEL="dacs"
MOUNT_POINT="${MOUNT_POINT:-/var/lib/ssd/cacheserver}"

log() {
  echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] $*"
}

dump_diag() {
  log "----- DEBUG (begin) -----"
  echo "hostname=$(hostname 2>/dev/null || echo '<unknown>')"
  lsblk -o NAME,TYPE,SIZE,FSTYPE,LABEL,MOUNTPOINT 2>/dev/null || true
  echo ""
  cat /proc/mdstat 2>/dev/null || true
  echo ""
  mdadm --detail --scan 2>/dev/null || true
  log "----- DEBUG (end) -----"
}

persist_config() {
  # Write mdadm.conf and fstab entries for reboot persistence.
  mkdir -p /etc/mdadm
  mdadm --detail --scan 2>/dev/null | tee /etc/mdadm/mdadm.conf >/dev/null || true
  if grep -q "^LABEL=${FS_LABEL}[[:space:]]" /etc/fstab 2>/dev/null; then
    # Ensure existing entry includes noatime (older entries may not).
    if ! grep "^LABEL=${FS_LABEL}[[:space:]]" /etc/fstab | grep -q "noatime"; then
      sed -i "/^LABEL=${FS_LABEL}[[:space:]]/s/defaults/defaults,noatime/" /etc/fstab
    fi
  else
    echo "LABEL=${FS_LABEL} ${MOUNT_POINT} ext4 defaults,noatime,nofail 0 2" >> /etc/fstab
  fi
  if command -v update-initramfs &>/dev/null; then
    update-initramfs -u
  fi
}

log "========== NVMe setup starting on $(hostname 2>/dev/null || echo '<unknown>') =========="

# --- Fast path: already mounted ------------------------------------------------
if mountpoint -q "${MOUNT_POINT}" 2>/dev/null; then
  log "${MOUNT_POINT} already mounted. Nothing to do."
  exit 0
fi

# --- Install mdadm if needed ---------------------------------------------------
if ! command -v mdadm &>/dev/null; then
  log "mdadm not found, installing..."
  if command -v tdnf &>/dev/null; then
    tdnf install -y mdadm e2fsprogs
  elif command -v apt-get &>/dev/null; then
    export DEBIAN_FRONTEND=noninteractive
    if ! { apt-get update >/dev/null && apt-get install -y mdadm e2fsprogs; }; then
      log "ERROR: Failed to install packages via apt-get"
      exit 1
    fi
  else
    log "ERROR: No supported package manager found."
    exit 1
  fi
fi

mkdir -p "${MOUNT_POINT}"

# --- Try to reassemble existing RAID arrays ------------------------------------
mdadm --assemble --scan 2>/dev/null || true

# --- Check for existing filesystem with our label (covers RAID and single-disk)
LABELED_DEV=$(blkid -L "${FS_LABEL}" 2>/dev/null || true)
if [[ -n "${LABELED_DEV}" ]]; then
  log "Found existing filesystem labeled '${FS_LABEL}' on ${LABELED_DEV}"
  mount -o noatime "${LABELED_DEV}" "${MOUNT_POINT}"
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
  if findmnt -S "$device" -n &>/dev/null; then
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
  if ! mkfs.ext4 -F -L "${FS_LABEL}" "${DEVICE}"; then
    log "ERROR: mkfs failed on ${DEVICE}."
    dump_diag
    exit 1
  fi
  mount -o noatime "${DEVICE}" "${MOUNT_POINT}"
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
if ! mkfs.ext4 -F -L "${FS_LABEL}" "${RAID_DEVICE}"; then
  log "ERROR: mkfs failed."
  dump_diag
  exit 1
fi

mount -o noatime "${RAID_DEVICE}" "${MOUNT_POINT}"
chmod 0777 "${MOUNT_POINT}"
persist_config

log "RAID0 setup complete."
