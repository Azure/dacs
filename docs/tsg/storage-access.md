# DACS Remote Storage Access Failure

## Symptoms

Remote reads, metadata calls, or writes fail with HTTP `401`/`403`,
authentication errors, token-acquisition failures, or storage endpoint
connection errors. Warm cached reads may succeed while new data fails.

## Description

Cache connectivity and remote Blob access are separate paths. Client workloads
need valid storage credentials/identity, data-plane permissions, and network
access to storage and any token endpoint. A cache-only smoke test does not
validate Blob authorization.

## Steps to Mitigate

1. Capture the failing operation, sanitized endpoint, HTTP/error code, and
   timestamp from the affected workload. Do not log tokens or SAS query strings.
   Inspect its pod events and application logs:

   ```bash
   export CLIENT_NAMESPACE="application-namespace"
   kubectl describe pod <client-pod> -n "$CLIENT_NAMESPACE"
   kubectl logs <client-pod> -n "$CLIENT_NAMESPACE" -c <client-container> --tail=200
   ```

2. Identify the authentication mode configured in the affected process. Check
   for stale overrides after credential rotation. Review the
   [client authentication guide](../../src/subsystems/storage/dacs_client/README.md#authentication)
   or [StorageIntercept guide](../../src/subsystems/storage/storage_intercept/README.md)
   for the actual client in use.
3. For SAS or supplied access tokens, check expiry, audience/scope, and required
   operation permissions locally without displaying the token. Rotate/update
   credentials through the normal secret-management workflow.
4. For managed/workload identity, have the identity administrator verify the
   intended identity, federation/service-account mapping, projected-token
   configuration, and storage data-plane roles. Do not grant broad subscription
   permissions as a shortcut.
5. For `403`, check both authorization and storage firewall/private-endpoint
   rules. For DNS/TLS/timeouts, check connectivity from the affected workload
   to storage and the applicable identity endpoint. Keep TLS validation enabled.
6. After correction, test a small permitted operation from the same workload
   identity/network context. A successful request from an administrator's
   workstation does not establish application access.

## Verification Steps

Verify remote metadata/read access for an object not already cached. If writes
are required, create a uniquely named test object through the intended write
mode and confirm it exists remotely. Check that authorization failures do not
recur across normal token refresh. Remove test data only through the approved
application/storage workflow.

## Contact Information

Contact the storage/identity administrator for access changes. For DACS help,
use [SUPPORT.md](../../SUPPORT.md) with redacted diagnostics from the
[escalation checklist](../installation-troubleshooting.md#escalation-checklist).
