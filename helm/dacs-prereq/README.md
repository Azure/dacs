# dacs-prereq

This Helm chart deploys a DaemonSet that sets up NVMe RAID arrays on nodes
before the DACS cache server is deployed.

## Overview

The chart runs an init container that:
1. Detects available NVMe disks on the node
2. Creates GPT partitions on each NVMe disk
3. Creates a RAID0 array using mdadm
4. Formats the array with ext4
5. Mounts it to the configured mount point
6. Adds an fstab entry for persistence across reboots

## Prerequisites

- Kubernetes cluster with NVMe-capable nodes (e.g., `Standard_L8s_v3`)
- Nodes labeled with `dacs.azure.com/processing-unit: cpu`
- Privileged container support

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
| `mountPoint` | Mount point for the RAID array | `/var/lib/ssd/cacheserver` |
| `image.repository` | Container image repository | `mcr.microsoft.com/azurelinux/base/core` |
| `image.tag` | Container image tag | `3.0` |
| `image.pullPolicy` | Image pull policy | `IfNotPresent` |
| `resources.limits.memory` | Memory limit | `512Mi` |
| `resources.requests.cpu` | CPU request | `100m` |
| `resources.requests.memory` | Memory request | `256Mi` |

## How It Works

The DaemonSet uses `nsenter` to run commands in the host namespace, which allows
it to:
- Access host block devices
- Run `gdisk` and `mdadm` commands
- Mount filesystems on the host
- Modify `/etc/fstab` for persistence

This approach avoids complex volume mounts and ensures the RAID setup persists
across pod restarts and node reboots.

## Uninstallation

```bash
helm uninstall dacs-prereq --namespace cache-server
```

Or using the Makefile:

```bash
make helm-prereq-uninstall
```

**Note:** Uninstalling the chart does not remove the RAID array or its data.
The array will persist on the node until manually removed.
