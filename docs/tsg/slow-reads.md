# DACS Slow Reads or Low Throughput

## Symptoms

Application reads take longer than usual, throughput drops, or request timeouts
increase even though CacheServer pods are Running.

## Description

Reads may involve cache discovery, cache transfer, or a remote storage fetch
on a cache miss. Cold caches, frequent eviction, client concurrency, CPU/memory
pressure, network limits, and remote storage latency can affect performance.
A healthy controller does not establish data-path performance.

## Steps to Mitigate

1. Record a representative request size, concurrency, dataset, duration, and
   previous baseline. Distinguish first reads from repeated reads of the same
   unchanged data.
2. Inspect affected pods and recent events:

   ```bash
   export NAMESPACE="dacs-system"
   export CACHE_NAME="cache-sample"
   kubectl get pods -n "$NAMESPACE" \
     -l "dacs.azure.com/cache-name=$CACHE_NAME" -o wide
   kubectl describe pod <cache-pod> -n "$NAMESPACE"
   kubectl logs <cache-pod> -n "$NAMESPACE" -c cacheserver --tail=200
   kubectl get events -n "$NAMESPACE" --sort-by=.metadata.creationTimestamp
   ```

3. If Metrics Server is installed, inspect resource use:

   ```bash
   kubectl top pods -n "$NAMESPACE"
   kubectl top nodes
   ```

   If the metrics API is unavailable, use your existing monitoring system.
   Review CPU throttling, memory pressure, and node/network utilization over the
   incident window rather than relying only on a point-in-time sample.
4. Compare client hit/miss and latency metrics. For continued misses, follow the
   [cache misses TSG](cache-misses.md). For timeouts or DNS errors, follow the
   [connectivity TSG](cache-connectivity.md).
5. Review `cache.cpuLimit`, `cache.memoryLimitMB`, `cache.serverThreads`, and any
   `cache.egressBandwidthLimitGbps` cap against the workload and node capacity.
   Change one setting at a time through the original Helm values.
6. For StorageIntercept, review `chunkOperationMaxConcurrency`,
   `maxConnsPerBlobClient`, and prefetch settings using its
   [configuration guide](../../src/subsystems/storage/storage_intercept/README.md).
   Increasing threads, buffers, or retries indiscriminately can worsen pressure.
7. If cache hits are fast but remote reads are slow, investigate storage/network
   latency or throttling rather than restarting healthy cache pods.

## Verification Steps

Repeat the same bounded workload under comparable conditions, recording cold
and warm read latency/throughput separately. Confirm the application's latency
or throughput target is met and timeout/resource-pressure signals do not grow.
Do not use the quickstart's elapsed time as a production performance threshold.

## Contact Information

Use [SUPPORT.md](../../SUPPORT.md) with a redacted workload baseline, observed
performance, and the [escalation checklist](../installation-troubleshooting.md#escalation-checklist).
