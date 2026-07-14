# dacs-prereq

Helm chart that deploys a DaemonSet to prepare NVMe data disks on each node
before the DACS cache server starts.

## Overview

The chart runs an init container that:

1. Checks if the cache directory is already mounted (fast-path exit).
2. Installs `mdadm` if needed and runs `mdadm --assemble --scan` to
   reassemble any previously created RAID arrays.
3. Looks for an existing filesystem labeled `dacs` (covers both
   RAID and single-disk setups from a prior boot).
4. If no prior setup exists, finds unused NVMe data disks and:
   - **Single disk:** formats ext4 directly (no RAID overhead).
   - **Multiple disks:** creates a RAID0 array, then formats ext4.
   All filesystems are labeled `dacs` for ownership detection.
5. Writes `mdadm.conf`, `fstab` (by label), and runs `update-initramfs`
   for reboot persistence.

## Prerequisites

- Kubernetes cluster with NVMe-capable nodes
- Nodes labeled with `dacs.azure.com/processing-unit: cpu`
- Privileged container support (the init container uses `nsenter`)

**OS support:** AzureLinux (tdnf) and Ubuntu (apt-get).

## Installation

```bash
helm upgrade --install dacs-prereq ./helm/dacs-prereq \
  --namespace cache-server --create-namespace \
  --wait --timeout 10m
```

Or using the Makefile:

```bash
make helm-prereq-install
```

## Configuration

| Parameter | Description | Default |
|-----------|-------------|---------|
| `nodeSelector` | Node selector for targeting NVMe nodes | `dacs.azure.com/processing-unit: cpu` |
| `tolerations` | Tolerations for the DaemonSet | `[]` |
| `mountPoint` | Mount point for the cache directory | `/var/lib/ssd/cacheserver` |
| `priorityClassName` | Pod priority class | `system-node-critical` |
| `updateStrategy.type` | DaemonSet update strategy | `RollingUpdate` |
| `updateStrategy.rollingUpdate.maxUnavailable` | Max unavailable during updates | `"10%"` |
| `nvmeRaidSetup.image.repository` | Init container image | `mcr.microsoft.com/azurelinux/base/core` |
| `nvmeRaidSetup.image.tag` | Init container image tag | `3.0` |
| `nvmeRaidSetup.resources.limits.cpu` | CPU limit for init container | `1` |
| `nvmeRaidSetup.resources.limits.memory` | Memory limit for init container | `512Mi` |
| `pauseContainer.image.repository` | Pause container image | `mcr.microsoft.com/azurelinux/base/core` |
| `pauseContainer.resources.limits.cpu` | CPU limit for pause container | `10m` |
| `pauseContainer.resources.limits.memory` | Memory limit for pause container | `16Mi` |

## How It Works

The DaemonSet uses `nsenter -t 1 -m -u -i -n -p` to execute the setup script
in the host PID/mount/network namespace.  This allows it to format disks,
run `mdadm`, mount filesystems, and write host config files (`mdadm.conf`,
`fstab`) without complex volume mounts.

### Reboot Behavior

**Managed OS (persistent root filesystem):**
The script writes `mdadm.conf` and `fstab` entries, so the OS auto-starts the
RAID array and mounts it on boot.  The DaemonSet re-runs and detects the
existing mount (fast-path exit).

**Ephemeral OS (root filesystem recreated on reboot):**
The root filesystem is recreated, so `mdadm.conf`, `fstab`, and installed
packages are all lost.  However, the RAID superblock metadata and the ext4
filesystem (labeled `dacs`) survive on the NVMe data disks.  On the
next run, the script:

1. Reinstalls `mdadm`.
2. Runs `mdadm --assemble --scan` to reassemble any RAID arrays.
3. Finds the filesystem by label (`blkid -L dacs`) and mounts it.
4. Rewrites `mdadm.conf`, `fstab`, and `initramfs`.

This means **cached data survives ephemeral OS reboots** as long as the NVMe
data disks are not replaced.

### Filesystem Label

The script labels all ext4 filesystems it creates with `dacs`.  This
label serves as ownership detection: on reboot, `blkid -L dacs` finds
the device regardless of whether it is a single disk or a reassembled RAID
array, enabling a unified fast-path mount.

## Uninstallation

```bash
helm uninstall dacs-prereq --namespace cache-server
```

Or using the Makefile:

```bash
make helm-prereq-uninstall
```

**Note:** Uninstalling the chart does not remove the RAID array, filesystem, or
cached data.  The array persists on the node until manually removed.
