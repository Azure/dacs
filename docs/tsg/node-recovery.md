# DACS Recovery After Node Disruption

## Symptoms

Cache access degrades after a reboot, drain, node upgrade, or node replacement.
Pods may remain Pending, clients may time out, or previously cached data may be
missing.

## Description

DACS uses node-local storage. An ephemeral cache is lost when its pod is removed.
NVMe data may survive a reboot if the disks survive, but node/disk replacement
does not preserve it. Storage preparation, placement, scaling, and client
discovery must all be checked after a disruption. Do not assume cache-only
writes are recoverable from remote storage.

## Steps to Mitigate

1. Record the maintenance event and identify affected nodes and pods:

   ```bash
   export NAMESPACE="dacs-system"
   export CACHE_NAME="cache-sample"
   kubectl get nodes -o wide
   kubectl get pods -n "$NAMESPACE" \
     -l "dacs.azure.com/cache-name=$CACHE_NAME" -o wide
   kubectl get events -n "$NAMESPACE" --sort-by=.metadata.creationTimestamp
   kubectl describe node <affected-node>
   ```

2. Have the cluster administrator resolve node health or capacity problems.
   Do not force-delete pods or uncordon nodes still undergoing maintenance.
3. For NVMe deployments, inspect prerequisite pods on affected/replacement nodes:

   ```bash
   kubectl get pods -n "$NAMESPACE" -l app.kubernetes.io/name=dacs-prereq -o wide
   kubectl logs <prereq-pod> -n "$NAMESPACE" -c nvme-raid-setup --tail=200
   ```

   Follow the [NVMe setup TSG](nvme-setup.md) to verify the actual host mount.
   Do not format disks to bypass an assembly or mount failure.
4. Verify replacement-node labels and eligibility. Use the
   [scheduling TSG](cacheserver-scheduling.md) for Pending pods and the
   [scaling TSG](cache-scaling.md) if node-count changes affect replicas.
5. Check client discovery refresh and discovered endpoint reachability using
   the [connectivity TSG](cache-connectivity.md). StorageIntercept discovery
   settings include `cacheServerDiscoveryEnabled`,
   `cacheServerDiscoveryEndpoint`, and `cacheServerDiscoveryIntervalInSeconds`.
6. Rewarm lost cache contents from an authoritative source at controlled
   concurrency. If cache-only or pending asynchronous writes were affected,
   preserve remaining evidence and escalate before deleting data or claiming
   recovery.

## Verification Steps

Confirm intended nodes are healthy, NVMe mounts exist where required, and the
expected cache pods become Ready. Verify discovery no longer directs traffic to
unavailable endpoints, then read representative application data and validate
its contents. Track cold-cache load separately from persistent failures.

## Contact Information

Contact the cluster administrator for node recovery. For DACS help, use
[SUPPORT.md](../../SUPPORT.md) and the
[escalation checklist](../installation-troubleshooting.md#escalation-checklist).
