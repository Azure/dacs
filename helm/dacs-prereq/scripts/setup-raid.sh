#!/bin/bash
# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.
#
# This script is executed via nsenter in the host namespace.
# See daemonset.yaml for the nsenter invocation.

set -euo pipefail

log() {
  echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] $*"
}

settle_block() {
  udevadm settle 2>/dev/null || true
  sleep 1
}

get_root_disk_name() {
  # Determine the underlying disk (e.g., nvme0n1, sda) that backs '/'.
  # Works for /dev/nvme0n1pX, /dev/sdaX, and common mapper devices.
  local root_source
  local root_name
  local pkname

  root_source=$(findmnt -n -o SOURCE / 2>/dev/null || true)
  if [ -z "${root_source}" ]; then
    echo ""
    return 0
  fi

  # Get a stable block-device name (no /dev/ prefix)
  root_name=$(lsblk -no NAME "${root_source}" 2>/dev/null || true)
  if [ -z "${root_name}" ]; then
    # If findmnt gave us something lsblk can't resolve, bail out safely.
    echo ""
    return 0
  fi

  # PKNAME is the parent disk if root is a partition; empty if already a disk.
  pkname=$(lsblk -no PKNAME "${root_source}" 2>/dev/null || true)
  if [ -n "${pkname}" ]; then
    echo "${pkname}"
    return 0
  fi

  echo "${root_name}"
  return 0
}

dump_diag() {
  # Low-noise diagnostics for pipeline-only debugging. Call only on busy/failure.
  log "----- DEBUG (begin) -----"
  if command -v hostname >/dev/null 2>&1; then
    echo "hostname=$(hostname)"
  else
    echo "hostname=<unknown>"
  fi
  echo "uname=$(uname -a 2>/dev/null || true)"
  echo ""
  echo "lsblk (name,type,size,fstype,mountpoint,pkname):"
  lsblk -o NAME,TYPE,SIZE,FSTYPE,MOUNTPOINT,PKNAME 2>/dev/null || true
  echo ""
  echo "lsblk -f:"
  lsblk -f 2>/dev/null || true
  echo ""
  echo "mount (filtered):"
  mount | grep -E 'nvme|/mnt/raid|/var/lib/ssd' || true
  echo ""
  echo "/proc/mdstat:"
  cat /proc/mdstat 2>/dev/null || true
  echo ""
  echo "mdadm --examine --scan:"
  mdadm --examine --scan 2>/dev/null || true
  echo ""
  echo "Device holders (nvme*):"
  for h in /sys/class/block/nvme*/holders/*; do
    [ -e "$h" ] || continue
    echo "$h"
  done
  log "----- DEBUG (end) -----"
}

device_in_use() {
  local device="$1"
  if lsblk -npo MOUNTPOINT "$device" 2>/dev/null | grep -qv '^[[:space:]]*$'; then
    return 0
  fi
  if [ -d "/sys/class/block/$(basename "$device")/holders" ] && [ "$(ls -A "/sys/class/block/$(basename "$device")/holders" 2>/dev/null | wc -l | xargs)" != "0" ]; then
    return 0
  fi
  return 1
}

if command -v hostname >/dev/null 2>&1; then
  NODE_NAME=$(hostname)
else
  NODE_NAME="<unknown>"
fi

log "========== NVMe setup starting on node ${NODE_NAME} =========="

# Install required tools if missing
if ! command -v mdadm >/dev/null 2>&1; then
  log "mdadm not found, installing required tools..."
  if command -v tdnf >/dev/null 2>&1; then
    log "Using tdnf package manager..."
    tdnf install -y mdadm util-linux e2fsprogs gdisk gawk || {
      log "ERROR: Failed to install packages via tdnf"
      exit 1
    }
  elif command -v apt-get >/dev/null 2>&1; then
    log "Using apt-get package manager..."
    export DEBIAN_FRONTEND=noninteractive
    apt-get update >/dev/null && apt-get install -y mdadm util-linux e2fsprogs gdisk gawk || {
      log "ERROR: Failed to install packages via apt-get"
      exit 1
    }
  else
    log "ERROR: Neither tdnf nor apt-get found. Cannot install mdadm."
    exit 1
  fi
  log "Package installation completed successfully"
else
  log "mdadm already installed, skipping package installation"
fi

MOUNT_POINT="${MOUNT_POINT:-/var/lib/ssd/cacheserver}"
marker_file="${MOUNT_POINT}/.nvme-setup-done"

mkdir -p "${MOUNT_POINT}"
chmod 0777 "${MOUNT_POINT}"

log "Ensured cache dir exists: ${MOUNT_POINT}"

# If a previous run already configured NVMe correctly, treat as success.
if mountpoint -q "${MOUNT_POINT}"; then
  log "Detected existing NVMe setup (${MOUNT_POINT} already mounted). Ensuring marker and permissions."
  mkdir -p "${MOUNT_POINT}"
  chmod 0777 "${MOUNT_POINT}"
  echo "nvme" > "${marker_file}"
  log "Wrote marker: ${marker_file}"
  log "NVMe setup already present; exiting successfully."
  exit 0
fi

root_disk=$(get_root_disk_name)
if [ -n "${root_disk}" ]; then
  log "Detected root disk: ${root_disk} (will be excluded from NVMe candidates)"
else
  log "WARN: Unable to detect root disk; will fall back to excluding nvme0n1 only"
fi

# Build candidate NVMe device list, excluding the detected root disk if possible.
nvme_devices=$(lsblk -nd -o NAME,TYPE | awk '$2=="disk" {print $1}' | grep '^nvme' || true)
if [ -n "${root_disk}" ]; then
  nvme_devices=$(echo "${nvme_devices}" | grep -vx "${root_disk}" || true)
else
  nvme_devices=$(echo "${nvme_devices}" | grep -v '^nvme0n1$' || true)
fi

log "Detected NVMe devices (excluding root): ${nvme_devices:-<none>}"

if [ -z "${nvme_devices}" ]; then
  log "No extra NVMe devices found. Using host OS disk path only."
  echo "no-nvme" > "${marker_file}"
  log "Wrote marker: ${marker_file}"
  exit 0
fi

log "Stopping any existing RAID arrays (best-effort)"
mdadm --stop /dev/md0 2>/dev/null || true
settle_block

raid_devices=""
for disk in ${nvme_devices}; do
  device="/dev/${disk}"
  part="${device}p1"

  log "Preparing ${device}"

  if device_in_use "${device}"; then
    log "WARN: ${device} appears busy. Attempting cleanup (best-effort)."
    dump_diag

    # Stop RAID (if present) and unmount the mountpoint first.
    umount -fl "${MOUNT_POINT}" 2>/dev/null || true
    mdadm --stop /dev/md0 2>/dev/null || true
    mdadm --remove /dev/md0 2>/dev/null || true
    settle_block

    # Legacy cleanup
    umount -fl "${part}" 2>/dev/null || true
    umount -fl "${device}" 2>/dev/null || true
    settle_block

    if device_in_use "${device}"; then
      log "ERROR: ${device} is still busy after cleanup; cannot proceed."
      echo "device-busy" > "${marker_file}"
      dump_diag
      exit 1
    fi
  fi

  # Wipe any existing signatures, then create a single Linux RAID partition.
  wipefs -af "${device}" 2>/dev/null || true
  settle_block

  log "Creating RAID partition on ${device}"
  # Create 1 partition covering whole disk with type code fd00 (Linux RAID)
  echo -e "n\n\n\n\nfd00\np\nw\nY" | gdisk "${device}" >/dev/null 2>&1 || true
  settle_block

  if [ ! -b "${part}" ]; then
    log "ERROR: Expected partition ${part} was not created or not visible."
    echo "partition-create-failed" > "${marker_file}"
    dump_diag
    exit 1
  fi

  raid_devices="${raid_devices} ${part}"
  log "Added RAID member partition: ${part}"
done

raid_devices=$(echo "${raid_devices}" | xargs)
device_count=$(echo "${raid_devices}" | wc -w | xargs)

if [ "${device_count}" -lt 1 ]; then
  log "ERROR: No RAID members available."
  echo "no-raid-members" > "${marker_file}"
  dump_diag
  exit 1
fi

log "Creating RAID0 with ${device_count} devices: ${raid_devices}"
if ! mdadm --create /dev/md0 --level=0 --raid-devices="${device_count}" ${raid_devices} --force; then
  log "ERROR: RAID creation failed."
  echo "raid-create-failed" > "${marker_file}"
  dump_diag
  exit 1
fi

sleep 5
settle_block

log "RAID status (/proc/mdstat):"
cat /proc/mdstat || true

log "Formatting /dev/md0 as ext4"
if ! mkfs.ext4 -F /dev/md0; then
  log "ERROR: mkfs failed."
  echo "mkfs-failed" > "${marker_file}"
  dump_diag
  exit 1
fi

log "Mounting /dev/md0 at ${MOUNT_POINT}"
mkdir -p "${MOUNT_POINT}"

if ! mount | grep -qE " on ${MOUNT_POINT} "; then
  if ! mount /dev/md0 "${MOUNT_POINT}"; then
    log "ERROR: Mount failed."
    echo "mount-failed" > "${marker_file}"
    dump_diag
    exit 1
  fi
else
  log "${MOUNT_POINT} already mounted"
fi

log "Ensuring permissions on NVMe-backed cache dir: ${MOUNT_POINT}"
mkdir -p "${MOUNT_POINT}"
chmod 0777 "${MOUNT_POINT}"

echo "nvme" > "${marker_file}"
log "Wrote marker: ${marker_file}"

log "========== Final validation =========="
echo "Mounts (filtered):"
mount | grep -E " ${MOUNT_POINT}" || true
echo ""
echo "df -h (filtered):"
df -h | grep -E "Filesystem|${MOUNT_POINT}" || true

log "NVMe setup completed successfully"
