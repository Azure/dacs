# DACS Client Write Failure or Unexpected Persistence

## Symptoms

Writes fail as read-only/unsupported operations, existing-object writes fail,
or an apparently successful write is not yet visible in remote Blob storage.

## Description

Write semantics depend on the API and selected mode. Core `DACSCacheClient`
disables writes when no mode is supplied; adapters can have different defaults.
`WRITE_CACHE_ONLY` does not persist data to Blob, and
`WRITE_CACHE_BLOB_ASYNC` uploads remotely in the background. API support for
append/overwrite also varies. Cache success alone is not proof of remote
durability.

## Steps to Mitigate

1. Identify the API, client version, selected write mode, object path, and exact
   exception. Inspect affected workload logs without exposing credentials:

   ```bash
   export CLIENT_NAMESPACE="application-namespace"
   kubectl logs <client-pod> -n "$CLIENT_NAMESPACE" -c <client-container> --tail=200
   ```

2. Compare the operation with the
   [client API and write-mode documentation](../../src/subsystems/storage/dacs_client/README.md).
   Do not assume every adapter supports append or overwriting existing objects.
   Reproduce with a uniquely named object rather than deleting existing data.
3. For durable remote writes, explicitly choose a mode appropriate to the
   application's contract. `WRITE_SYNC` writes cache and Blob synchronously;
   `WRITE_BLOB_ONLY` bypasses cache. Do not switch production semantics without
   assessing the application impact.
4. For async mode, inspect background upload errors and use documented
   close/shutdown handling. Do not terminate the client or remove cache data
   while uploads may be pending. Independently confirm remote persistence.
5. For StorageIntercept configuration, review `enableWrite`, `enableWriteRemote`,
   and `enableWriteRemoteAsync`. Consistent mode with writes requires versioned
   keys, remote writes enabled, and asynchronous remote writes disabled; see its
   [configuration guide](../../src/subsystems/storage/storage_intercept/README.md).
6. Follow the [storage access TSG](storage-access.md) for remote authorization
   errors and [capacity TSG](cache-capacity.md) for cache-space failures.

## Verification Steps

Write a small uniquely named object using the intended API/mode, then close it
through the documented lifecycle. Verify its length and contents from the
authoritative remote store using an approved client, not only a cached read.
For intentional cache-only mode, verify cache access and explicitly document
that remote persistence is not expected.

## Contact Information

Use [SUPPORT.md](../../SUPPORT.md) with the API, write mode, sanitized exception,
and [escalation checklist](../installation-troubleshooting.md#escalation-checklist).
