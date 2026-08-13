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

#
# Tears down the DACS NVMe RAID array and mounts created by setup-raid.sh.
# Runs via privileged kubectl pods with chroot into the host filesystem.
#
# WARNING: This is destructive - it unmounts, stops RAID arrays, wipes
# superblocks, and removes fstab/mdadm.conf entries on ALL nodes.

set -euo pipefail

if ! command -v jq &>/dev/null; then
    echo "Error: jq is required but not installed" >&2
    exit 1
fi

readonly FS_LABEL="dacs"
readonly MOUNT_POINT="${MOUNT_POINT:-/var/lib/ssd/cacheserver}"
readonly IMAGE="mcr.microsoft.com/azurelinux/base/core:3.0"
readonly NAMESPACE="${NAMESPACE:-default}"

FORCE=false
while [[ $# -gt 0 ]]; do
    case $1 in
        -f|--force) FORCE=true; shift ;;
        -h|--help)
            echo "Usage: $(basename "$0") [-f|--force]"
            echo "  -f, --force   Skip confirmation prompt"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo "WARNING: This is a DESTRUCTIVE operation on ALL nodes in the current cluster."
echo ""
echo "  It will:"
echo "    - Unmount ${MOUNT_POINT}"
echo "    - Stop and zero mdadm RAID arrays named '${FS_LABEL}'"
echo "    - Wipe filesystem signatures on member devices"
echo "    - Remove fstab and mdadm.conf entries"
echo ""
echo "  Any cached data will be permanently lost."
echo ""

if [[ "${FORCE}" != "true" ]]; then
    read -rp "Continue? [y/N]: " confirmation
    if [[ "${confirmation}" != "y" && "${confirmation}" != "Y" ]]; then
        echo "Cancelled."
        exit 0
    fi
fi

NODES=$(kubectl get nodes -o jsonpath='{.items[*].metadata.name}')
if [[ -z "${NODES}" ]]; then
    echo "No nodes found."
    exit 1
fi

# The cleanup commands that run inside chroot on each node.
# shellcheck disable=SC2016
CLEANUP_SCRIPT='
set -euo pipefail

MOUNT_POINT="'"${MOUNT_POINT}"'"
FS_LABEL="'"${FS_LABEL}"'"

echo "[teardown] Starting on $(hostname)"

# Unmount
if mountpoint -q "${MOUNT_POINT}" 2>/dev/null; then
    echo "[teardown] Unmounting ${MOUNT_POINT}"
    umount -f "${MOUNT_POINT}" || umount -l "${MOUNT_POINT}"
else
    echo "[teardown] ${MOUNT_POINT} not mounted"
fi

# Find the device by label before we wipe anything
LABELED_DEV=$(blkid -L "${FS_LABEL}" 2>/dev/null || true)

# Stop RAID arrays
if command -v mdadm &>/dev/null; then
    for md in /dev/md/*; do
        [ -e "$md" ] || continue
        if mdadm --detail "$md" 2>/dev/null | grep -q "${FS_LABEL}"; then
            echo "[teardown] Stopping RAID array $md"
            MEMBERS=$(mdadm --detail "$md" 2>/dev/null | awk "/\/dev\/nvme/{print \$NF}")
            mdadm --stop "$md" 2>/dev/null || true
            # Zero superblocks on member devices
            for dev in ${MEMBERS}; do
                echo "[teardown] Zeroing superblock on $dev"
                mdadm --zero-superblock "$dev" || echo "[teardown] WARNING: zero-superblock failed on $dev"
            done
        fi
    done
fi

# Wipe filesystem label from single-disk setups
if [[ -n "${LABELED_DEV}" && -e "${LABELED_DEV}" ]]; then
    echo "[teardown] Wiping filesystem signature on ${LABELED_DEV}"
    wipefs -f -a "${LABELED_DEV}" || echo "[teardown] WARNING: wipefs failed on ${LABELED_DEV}"
fi

# Clean up fstab
if grep -q "LABEL=${FS_LABEL}" /etc/fstab 2>/dev/null; then
    echo "[teardown] Removing fstab entry"
    sed -i "\|LABEL=${FS_LABEL}|d" /etc/fstab
fi

# Clean up mdadm.conf
if [ -f /etc/mdadm/mdadm.conf ]; then
    if grep -q "${FS_LABEL}" /etc/mdadm/mdadm.conf 2>/dev/null; then
        echo "[teardown] Cleaning mdadm.conf"
        sed -i "\|${FS_LABEL}|d" /etc/mdadm/mdadm.conf
    fi
fi

# Remove mount point directory
if [ -d "${MOUNT_POINT}" ]; then
    echo "[teardown] Removing ${MOUNT_POINT}"
    rmdir "${MOUNT_POINT}" 2>/dev/null || true
fi

echo "[teardown] Done on $(hostname)"
'

for NODE in ${NODES}; do
    echo "--- Node: ${NODE} ---"

    if ! kubectl get node "${NODE}" &>/dev/null; then
        echo "Node ${NODE} not found, skipping."
        continue
    fi

    POD="dacs-teardown-${RANDOM}"

    OVERRIDES=$(jq -n \
        --arg node "${NODE}" \
        --arg image "${IMAGE}" \
        --arg script "${CLEANUP_SCRIPT}" \
        '{
            spec: {
                nodeName: $node,
                hostPID: true,
                containers: [{
                    securityContext: { privileged: true },
                    image: $image,
                    name: "teardown",
                    command: ["chroot", "/proc/1/root", "bash", "-c", $script]
                }],
                restartPolicy: "Never"
            }
        }')

    kubectl run --namespace "${NAMESPACE}" --rm -i \
        --image "${IMAGE}" --overrides="${OVERRIDES}" --restart=Never "${POD}" 2>&1 || {
        echo "ERROR: Teardown failed on node ${NODE}"
        continue
    }

    echo "Teardown complete on ${NODE}"
    echo ""
done

echo "Teardown finished on all nodes."
