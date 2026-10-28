# DACS Cache Capacity or Disk Pressure

## Symptoms

Cache writes fail with `No space left on device`, nodes report `DiskPressure`,
pods are evicted, or frequent cache eviction causes repeated remote reads.

## Description

Configured cache capacity and physical filesystem free space are different
limits. Other node workloads, logs, or inode exhaustion can consume the backing
filesystem. DACS eviction controls cache retention; it does not clean unrelated
host data. Ephemeral storage also consumes node-local capacity.

## Steps to Mitigate

1. Inspect the Cache, affected pod, node, and eviction logs:

   ```bash
   export NAMESPACE="dacs-system"
   export CACHE_NAME="cache-sample"
   kubectl describe cache "$CACHE_NAME" -n "$NAMESPACE"
   kubectl describe pod <cache-pod> -n "$NAMESPACE"
   kubectl describe node <cache-node>
   kubectl logs <cache-pod> -n "$NAMESPACE" -c cacheserver --tail=300
   ```

2. With approved host access, check the backing filesystem and inodes using
   read-only commands. Substitute the configured host path:

   ```bash
   findmnt --mountpoint /var/lib/ssd/cacheserver
   df -h /var/lib/ssd/cacheserver
   df -i /var/lib/ssd/cacheserver
   ```

   For ephemeral mode, inspect the node filesystem backing the pod's `emptyDir`
   through your cluster's normal node diagnostics.
3. If the expected NVMe mount is absent, use the [NVMe setup TSG](nvme-setup.md).
   Do not assume an existing directory is backed by the data disk.
4. Review `cache.serverSizeInGB` and `cache.eviction` settings, including
   `enableAccessTimeEviction`, `evictionMaintainFreeSpace`, `sizeThreshold`,
   `sizeTarget`, and `passFrequency`. Keep the target below the threshold and
   leave filesystem headroom. Inspect logs for invalid-setting warnings and
   eviction progress.
5. If the working set exceeds safe capacity, add suitable capacity or revise
   retention/sizing through the original values file. Have the administrator
   address unrelated host disk use; do not delete arbitrary host/cache files.
6. Before reducing `cache.serverSizeInGB`, assess data impact: the startup
   space-check script can clear the cache directory when existing contents exceed
   the configured limit. Confirm authoritative remote copies and pending writes
   before any size change, eviction-policy change, or restart.

## Verification Steps

Confirm filesystem space and inodes have headroom, node disk pressure clears,
and affected pods remain stable. Run a bounded application write/read and
observe capacity and eviction through at least the configured eviction interval.
Verify pressure does not recur immediately under the representative workload.

## Contact Information

Contact the cluster/storage administrator for host capacity. For DACS help,
use [SUPPORT.md](../../SUPPORT.md) and the
[escalation checklist](../installation-troubleshooting.md#escalation-checklist).
