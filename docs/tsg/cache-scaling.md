# DACS Unexpected Cache Scaling

## Symptoms

The cache does not scale after node changes, replica counts differ from
expectations, new replicas stay Pending, or a dynamically scaled cache drops to
zero replicas.

## Description

DACS supports static counts and percentage-based dynamic scaling. Dynamic
scaling uses schedulable matching nodes, server bounds, tolerance, and
stabilization. Its `minServers` is an all-or-nothing minimum: a calculated count
below the minimum can become zero rather than being raised to the minimum.
This is not CPU-based autoscaling.

## Steps to Mitigate

1. Inspect desired configuration, scaling status, eligible nodes, and logs:

   ```bash
   export NAMESPACE="dacs-system"
   export CACHE_NAME="cache-sample"
   kubectl get cache "$CACHE_NAME" -n "$NAMESPACE" -o yaml
   kubectl get statefulset "$CACHE_NAME" -n "$NAMESPACE"
   kubectl get nodes -L dacs.azure.com/processing-unit
   kubectl logs deployment/dacs-manager -n "$NAMESPACE" -c manager --tail=200
   ```

   Use your actual Deployment name if `namePrefix` differs from `dacs`.
   Review CR output before sharing it.
2. Confirm the intended mode. Static mode uses `cache.scaling.static.numServers`.
   If dynamic configuration is supplied through the chart, it takes precedence
   over the chart's default static configuration.
3. For dynamic mode, count schedulable nodes matching the configured selector
   and calculate `floor(nodeCount * percentageOfNodesToUse / 100)`. Apply
   `maxServers` and the documented all-or-nothing `minServers` behavior.
4. Check controller tolerance and stabilization settings. Defaults are 10%
   tolerance and 15-minute windows; node churn can keep resetting the window.
   Small count changes may be intentionally suppressed.
5. Correct the intended mode, bounds, percentage, or selector through the
   original Helm values. Do not repeatedly restart the controller or disable
   stabilization just to force a change.
6. If desired replicas are correct but pods remain Pending, use the
   [scheduling TSG](cacheserver-scheduling.md). Host-storage pods require separate
   eligible nodes because of pod anti-affinity.

See the [scaling guide](../../src/subsystems/cache-controller/docs/controller/scaling.md)
for the exact calculation and stabilization behavior.

## Verification Steps

Compare Cache scaling status and StatefulSet desired/ready replicas after the
applicable stabilization window with stable node eligibility. Confirm the count
matches the documented calculation and all requested pods become Ready.
Check client discovery and reads after a server-set change.

## Contact Information

Use [SUPPORT.md](../../SUPPORT.md) with scaling configuration, node-count history,
and the [escalation checklist](../installation-troubleshooting.md#escalation-checklist).
