# dacs-prereq

Helm chart that deploys a DaemonSet to prepare NVMe data disks on each node
for the DACS cache server.

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

Both parent charts bundle this chart as an optional dependency, disabled by
default. Enable it when the release should prepare NVMe storage:

```bash
helm dependency build ./helm/cache-server
helm upgrade --install cache-server ./helm/cache-server \
  --namespace cache-server --create-namespace \
  --set cacheServerPrereq.enabled=true \
  --set cacheServerPrereq.nvmeRaidSetup.image.repository="<registry>/dacs/cache-setup" \
  --set-string cacheServerPrereq.nvmeRaidSetup.image.tag="<image-version>" \
  --atomic --timeout 15m
```

For the controller chart, use
`make helm-dacs-install CACHE_SERVER_PREREQ_ENABLED=true`. Wait for storage
preparation to succeed before creating Cache resources in a separate operation.
The rollout hook runs at the end of the release and does not block workloads
created in the same operation. Minikube leaves prerequisites disabled.

The chart is also published independently and can be installed directly with
`make helm-prereq-install`. Published parent charts contain the build registry's
`dacs/cache-setup` image and stamp the bundled chart's `appVersion` with the
matching container version.

Build the image with `make images`, or build it independently:

```bash
docker build -f Docker/CacheSetup.Dockerfile -t cache-setup:local Docker
```

Publish the image before deploying the chart, and select the same immutable
image version as the rest of the deployment.

## Configuration

| Parameter | Description | Default |
|-----------|-------------|---------|
| `global.registry` | Registry host with optional project prefix, without a URL scheme | `""` |
| `global.registryMode` | `replace` redirects prerequisite image hosts; `legacy` leaves repositories unchanged | `legacy` |
| `global.imagePullSecrets` | Existing registry credential Secret references in the release namespace | `[]` |
| `name` | Base name for the DaemonSet and RBAC resources | `dacs-prereq` |
| `nodeSelector` | Node selector for targeting NVMe nodes | `dacs.azure.com/processing-unit: cpu` |
| `tolerations` | Tolerations for the DaemonSet | `[]` |
| `mountPoint` | Mount point for the cache directory | `/var/lib/ssd/cacheserver` |
| `priorityClassName` | Pod priority class | `system-node-critical` |
| `updateStrategy.type` | DaemonSet update strategy | `RollingUpdate` |
| `updateStrategy.rollingUpdate.maxUnavailable` | Max unavailable during updates | `"10%"` |
| `nvmeRaidSetup.image.repository` | Init container image | `cache-setup` |
| `nvmeRaidSetup.image.tag` | Init container image tag; empty uses chart appVersion | `""` |
| `rolloutCheck.timeoutSeconds` | Current DaemonSet rollout timeout | `600` |
| `rolloutCheck.tolerations` | Tolerations for the rollout-check Job, independent of the DaemonSet | `CriticalAddonsOnly` with operator `Exists` |
| `rolloutCheck.affinity` | Affinity for the rollout-check Job | Linux amd64/arm64 nodes, with a soft preference for AKS system nodes |
| `rolloutCheck.image.repository` | Rollout-check kubectl image | `mcr.microsoft.com/oss/v2/kubernetes/kubectl` |
| `rolloutCheck.image.tag` | Rollout-check kubectl image tag | `v1.35.9` |
| `nvmeRaidSetup.resources.limits.cpu` | CPU limit for init container | `1` |
| `nvmeRaidSetup.resources.limits.memory` | Memory limit for init container | `2Gi` |
| `pauseContainer.image.repository` | Pause container image | `mcr.microsoft.com/azurelinux/base/core` |
| `pauseContainer.resources.limits.cpu` | CPU limit for pause container | `10m` |
| `pauseContainer.resources.limits.memory` | Memory limit for pause container | `16Mi` |

The init container uses a 512 MiB memory request and a 2 GiB limit.

### Private Registries

Set these values at the parent chart's top level. Helm passes `global` values
to the bundled prerequisite chart:

```yaml
cacheServerPrereq:
  enabled: true
global:
  registry: registry.example.com/dacs
  registryMode: replace
  imagePullSecrets:
    - name: registry-auth
```

With `replace`, setup, pause, and rollout-check images use this registry while
preserving repository subpaths and tags. Mirror all three images there.
With `legacy`, their repositories stay unchanged. For mixed registries, leave
`global.registry` empty and override each prerequisite image directly.

Create referenced Secrets in the release namespace before installation.
An empty list uses node-provided image authentication. These settings cover
the prerequisites and rollout Job, not controller or generated CacheServer
image authentication.

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

### Rollout Verification

A post-install/post-upgrade Job runs [rollout-check.sh](scripts/rollout-check.sh)
to verify the current DaemonSet rollout on at least one targeted node.
Its dedicated ServiceAccount, Role, and RoleBinding are namespace-scoped.
DaemonSet access is restricted to the prerequisite's name; pod and event lists
are used for failure diagnostics.

The Job's scheduling defaults match the controller Deployment: it tolerates
`CriticalAddonsOnly`, requires Linux amd64/arm64 nodes, and prefers nodes
labeled `kubernetes.azure.com/mode=system`. The system-pool preference is soft,
so it can also run on other eligible nodes. Other taints still need matching
tolerations.
Configure `rolloutCheck.tolerations` and `rolloutCheck.affinity` independently
of the DaemonSet's scheduling settings. For a bundled chart, use the
`cacheServerPrereq.rolloutCheck` values.

Rollout commands share a 600-second budget by default, with up to three
attempts and five seconds between failures. The final node-count lookup has
a separate 40-second budget. Process-level timeouts preserve automatic
in-cluster API authentication. Diagnostics have a ten-second limit per command.
The Job deadline is the rollout timeout plus 180 seconds. Use a Helm timeout
longer than that deadline; 15 minutes covers the defaults. Persistent failures
fail the hook and an atomic Helm release.

### Migrating a Standalone Release

Uninstall a standalone `dacs-prereq` release before enabling the dependency in
a parent release in the same namespace. Helm cannot adopt another release's
resources. Host arrays, filesystems, and data remain in place.

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

Uninstall the parent release to remove its bundled prerequisite resources:

```bash
helm uninstall cache-server --namespace cache-server
```

For a directly installed prerequisite release:

```bash
helm uninstall dacs-prereq --namespace cache-server
```

**Note:** Uninstalling the chart does not remove the RAID array, filesystem, or
cached data.  The array persists on the node until manually removed.
