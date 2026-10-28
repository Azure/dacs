# DACS Cache Discovery or Client Connectivity Failure

## Symptoms

CacheServer pods are Ready, but a client cannot discover servers or connect.
The first request may fail with DNS resolution errors, connection errors,
timeouts, or a failed quickstart smoke-test Job.

## Description

Clients use the cache discovery Service to locate CacheServers and then reach
cache endpoints. Successful discovery alone does not prove endpoint
connectivity. Service DNS normally resolves inside the cluster, not directly
from a workstation.

## Steps to Mitigate

1. Inspect pods, the discovery Service, endpoints, and network policies:

   ```bash
   export NAMESPACE="dacs-system"
   export CACHE_NAME="cache-sample"
   kubectl get pods -n "$NAMESPACE" \
     -l "dacs.azure.com/cache-name=$CACHE_NAME" -o wide
   kubectl get service "${CACHE_NAME}-discovery" -n "$NAMESPACE"
   kubectl get endpointslices -n "$NAMESPACE" \
     -l "kubernetes.io/service-name=${CACHE_NAME}-discovery"
   kubectl get networkpolicy -n "$NAMESPACE"
   ```

2. For a missing Service, inspect
   [Cache creation and reconciliation](cache-creation.md).
3. For no ready endpoints, restore
   [CacheServer readiness](cacheserver-startup.md) and verify Service selectors.
4. Use the correct namespace in the client endpoint, for example
   `cache-sample-discovery.dacs-system.svc.cluster.local`. Substitute the cluster
   DNS suffix if it differs from `cluster.local`.
5. Test DNS from the affected workload using its available tools. Review policies
   in both client and cache namespaces, DNS egress, and access to discovered
   cache endpoints on the configured data port (default TCP `9065`).
6. For failed quickstart Jobs, inspect events and logs:

   ```bash
   kubectl describe job dacs-smoke-upload -n "$NAMESPACE"
   kubectl logs job/dacs-smoke-upload -n "$NAMESPACE"
   kubectl describe job dacs-smoke-download -n "$NAMESPACE"
   kubectl logs job/dacs-smoke-download -n "$NAMESPACE"
   ```

   These names apply to the quickstart; use your actual Job names otherwise.
   Inspect the affected Job pod for startup failures. Use matching client images
   and the same key prefix for upload and download.

## Verification Steps

Run the [quickstart smoke test](../quickstart.md#run-the-end-to-end-smoke-test).
Both Jobs must report Complete, their logs must contain
`All iterations succeeded: true`, and the checksum-validated download must
succeed. A deployed Helm release or a Running pod alone does not establish
end-to-end success.

## Contact Information

Contact the cluster/network administrator for DNS and policy issues.
For DACS help, use [SUPPORT.md](../../SUPPORT.md) and include redacted diagnostics
from the [escalation checklist](../installation-troubleshooting.md#escalation-checklist).
