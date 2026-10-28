# DACS Repeated Cache Misses

## Symptoms

Repeated reads of the same data continue fetching from remote storage, the
client reports a low hit rate, or a previously warm workload becomes cold.

## Description

Cache contents can change after eviction, expiry, server-set changes, pod/node
replacement, or source-version changes. Client settings can also disable caching
or direct requests to a different cache. A successful read may be served remotely
and does not prove a cache hit.

## Steps to Mitigate

1. Identify a small, unchanged object and capture client hit/miss metrics before
   and after reading it twice. Allow pending asynchronous cache population to
   complete before interpreting the second read.
2. Inspect cache placement, restarts, and configuration:

   ```bash
   export NAMESPACE="dacs-system"
   export CACHE_NAME="cache-sample"
   kubectl get pods -n "$NAMESPACE" \
     -l "dacs.azure.com/cache-name=$CACHE_NAME" -o wide
   kubectl describe cache "$CACHE_NAME" -n "$NAMESPACE"
   kubectl logs <cache-pod> -n "$NAMESPACE" -c cacheserver --tail=200
   ```

3. Review the affected client's configuration locally without printing secrets.
   For StorageIntercept, verify `cacheEnable`, the intended server/discovery
   endpoint, and the actual `cacheServerPort`. Do not assume the client's default
   port matches the chart's default TCP `9065`.
4. Check for cache-upload errors and unexpected expiry/group settings such as
   `cacheKeyExpirySeconds` and `cacheKeyGroupId`. Review
   [StorageIntercept configuration](../../src/subsystems/storage/storage_intercept/README.md).
5. Compare the incident time with scaling, rolling updates, node replacement,
   and eviction logs. For rapid churn or a working set larger than capacity,
   use the [capacity TSG](cache-capacity.md).
6. Verify that object paths and versions are stable. Versioned keys intentionally
   distinguish source versions; do not disable versioning or consistency checks
   merely to improve apparent hit rate.
7. If discovery is stale or points to another cache, follow the
   [connectivity TSG](cache-connectivity.md).

## Verification Steps

Read the same unchanged object repeatedly with the same client configuration
and a stable server set. Confirm successful cache population followed by cache
hits in client metrics, and verify returned data. Do not require a 100% hit rate
for an entire workload that includes new data, metadata, or version changes.

## Contact Information

Use [SUPPORT.md](../../SUPPORT.md) with redacted hit/miss observations and the
[escalation checklist](../installation-troubleshooting.md#escalation-checklist).
