# dacs-prereq

Helm chart that deploys a DaemonSet to prepare NVMe data disks on each node
before the DACS cache server starts.

## Overview

The chart runs an init container that:

1. Checks if the cache directory is already mounted (fast-path exit).
2. Uses the cache-setup image's tools and runs `mdadm --assemble --scan` to
   reassemble any previously created RAID arrays.
3. Looks for an existing filesystem labeled `dacs` (covers both
   RAID and single-disk setups from a prior boot).
4. If no prior setup exists, finds unused NVMe data disks and:
   - **Single disk:** formats ext4 directly (no RAID overhead).
   - **Multiple disks:** creates a RAID0 array, then formats ext4.
   All filesystems are labeled `dacs` for ownership detection.
5. Writes host `mdadm.conf` and `fstab` (by label), and runs the host's
   `update-initramfs` when available.

## Prerequisites

- Kubernetes cluster with NVMe-capable nodes
- Nodes labeled with `dacs.azure.com/processing-unit: cpu`
- Privileged container and host PID/device access

**OS support:** AzureLinux and Ubuntu hosts. RAID and filesystem tools are
provided by the cache-setup image; no packages are installed on the host.

## Installation

```bash
helm upgrade --install dacs-prereq ./helm/dacs-prereq \
  --namespace cache-server --create-namespace \
  --set nvmeRaidSetup.image.repository="<registry>/dacs/cache-setup" \
  --set-string nvmeRaidSetup.image.tag="<image-version>" \
  --wait --timeout 10m
```

Or using the Makefile:

```bash
make helm-prereq-install DOCKER_REGISTRY="<registry>" BUILD_NUMBER="<image-version>"
```

Build the image with `make images`, or build it independently:

```bash
docker build -f Docker/CacheSetup.Dockerfile -t cache-setup:local Docker
```

Publish the image before deploying the chart, and select the same immutable
image version as the rest of the deployment.

## Configuration

| Parameter | Description | Default |
|-----------|-------------|---------|
| `nodeSelector` | Node selector for targeting NVMe nodes | `dacs.azure.com/processing-unit: cpu` |
| `tolerations` | Tolerations for the DaemonSet | `[]` |
| `mountPoint` | Mount point for the cache directory | `/var/lib/ssd/cacheserver` |
| `priorityClassName` | Pod priority class | `system-node-critical` |
| `updateStrategy.type` | DaemonSet update strategy | `RollingUpdate` |
| `updateStrategy.rollingUpdate.maxUnavailable` | Max unavailable during updates | `"10%"` |
| `nvmeRaidSetup.image.repository` | Init container image | `cache-setup` |
| `nvmeRaidSetup.image.tag` | Init container image tag; empty uses chart appVersion | `""` |
| `nvmeRaidSetup.resources.limits.cpu` | CPU limit for init container | `1` |
| `nvmeRaidSetup.resources.limits.memory` | Memory limit for init container | `2Gi` |
| `pauseContainer.image.repository` | Pause container image | `mcr.microsoft.com/azurelinux/base/core` |
| `pauseContainer.resources.limits.cpu` | CPU limit for pause container | `10m` |
| `pauseContainer.resources.limits.memory` | Memory limit for pause container | `16Mi` |

The init container uses a 512 MiB memory request and a 2 GiB limit.

## How It Works

The DaemonSet uses `hostPID`, a privileged init container, and a host `/dev`
mount. It enters only the host UTS namespace to use the node hostname for RAID
operations while retaining the image's tools. The script explicitly targets
the host mount namespace and writes host configuration through `/proc/1/root`.
Host `update-initramfs`, when available, runs in the host mount namespace.

Both single-disk and RAID0 formatting explicitly disable `orphan_file` and
`metadata_csum_seed`. This keeps the image-created ext4 filesystems compatible
with Ubuntu 22.04's e2fsck 1.46.5. The host `fstab` entry uses fsck pass `2`, so
the host can check the cache filesystem at boot.

### Reboot Behavior

**Managed OS (persistent root filesystem):**
The script retains host `mdadm.conf` and `fstab` entries. Boot-time assembly
still depends on the node's existing MD support and boot configuration; the
container does not install host packages. When setup runs again, it reuses an
existing mount or attempts to assemble and mount the labeled filesystem.

**Ephemeral OS (root filesystem recreated on reboot):**
If the node root filesystem is recreated, host configuration may be lost.
When the NVMe disks and their metadata are retained, the next setup run:

1. Uses the cache-setup image's preinstalled tools.
2. Runs `mdadm --assemble --scan` to reassemble any RAID arrays.
3. Finds the filesystem by label (`blkid -L dacs`) and mounts it.
4. Rewrites host `mdadm.conf` and `fstab`, and updates initramfs if supported.

Recovery depends on retention of the local disks and a successful setup run;
this chart does not guarantee local-disk retention across node lifecycle events.

### Upgrade Compatibility

Existing cache filesystems are reused without changing their features.
Upgrading the chart does not repair filesystems created with `orphan_file`
enabled. On Ubuntu 22.04, those filesystems need separate remediation before
the next node reboot to avoid a boot-time fsck failure.

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
