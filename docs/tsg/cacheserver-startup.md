# DACS CacheServer Startup Failure

## Symptoms

A scheduled CacheServer pod restarts, reports `CrashLoopBackOff` or `OOMKilled`,
or remains unready. Logs may show mount/permission errors or
`No space left on device`.

## Description

CacheServers need valid configuration, accessible cache storage, and sufficient
resources. A Running pod is not necessarily Ready. `cache.serverSizeInGB`
configures cache capacity; it is not the container memory limit.

## Steps to Mitigate

1. Capture events and logs before replacing the pod. Replace `<cache-pod>` with
   the affected pod:

   ```bash
   export NAMESPACE="dacs-system"
   export CACHE_NAME="cache-sample"
   kubectl get pods -n "$NAMESPACE" \
     -l "dacs.azure.com/cache-name=$CACHE_NAME" -o wide
   kubectl describe pod <cache-pod> -n "$NAMESPACE"
   kubectl logs <cache-pod> -n "$NAMESPACE" --all-containers --tail=200
   ```

2. For restarted containers, also capture previous logs:

   ```bash
   kubectl logs <cache-pod> -n "$NAMESPACE" --all-containers --previous --tail=200
   ```

   If no previous container exists, that error is not an additional failure.
3. For `OOMKilled`, review `cache.memoryLimitMB`, thread count, and workload needs.
   Update the original deployment values if sizing changes are required.
4. For mount/permission errors, verify the configured host path, mount, and
   expected ownership. Follow the [NVMe setup TSG](nvme-setup.md) for host
   storage; do not apply broad permissions changes blindly.
5. For `No space left on device` or `DiskPressure`, inspect the backing filesystem
   and node ephemeral storage. Reduce cache sizing or add capacity; do not delete
   unrelated host data.
6. For probe failures or process exits, use the actual startup error to check
   ports, configuration, and chart/image compatibility before restarting.

Replacing an ephemeral cache pod loses its cached data. Do not delete the Cache,
namespace, or storage as a diagnostic step.

## Verification Steps

```bash
kubectl wait -n "$NAMESPACE" --for=condition=Ready pod \
  --selector="dacs.azure.com/cache-name=$CACHE_NAME" --timeout=5m
kubectl get pods -n "$NAMESPACE" \
  -l "dacs.azure.com/cache-name=$CACHE_NAME"
```

Confirm all expected CacheServer pods are present and Ready and restart counts
stop increasing. Run the
[quickstart smoke test](../quickstart.md#run-the-end-to-end-smoke-test) to verify
upload and checksum-validated download.

## Contact Information

Use the channels in [SUPPORT.md](../../SUPPORT.md) and include redacted
diagnostics from the [escalation checklist](../installation-troubleshooting.md#escalation-checklist).
