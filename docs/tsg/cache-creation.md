# DACS Cache Creation Failure

## Symptoms

The Cache resource is missing, `no matches for kind "Cache"` is reported, or
the Cache exists but no CacheServer StatefulSet or pods appear.

## Description

The `dacs` chart installs the Cache CRD and controller. A Cache custom resource
must then request cache infrastructure. The chart's `cache.enabled` value
defaults to `false`; installing the controller alone does not create a cache.

## Steps to Mitigate

1. Inspect the CRD, chart values, Cache status, and controller logs:

   ```bash
   export NAMESPACE="dacs-system"
   export RELEASE="dacs"
   export CACHE_NAME="cache-sample"
   kubectl get crd caches.storage.azure.com
   kubectl get cache -n "$NAMESPACE"
   helm get values "$RELEASE" -n "$NAMESPACE" --all
   kubectl describe cache "$CACHE_NAME" -n "$NAMESPACE"
   kubectl logs deployment/dacs-manager -n "$NAMESPACE" -c manager --tail=200
   ```

   Substitute your actual names; `dacs-manager` assumes `namePrefix=dacs`.
   A missing Cache or CRD may produce `NotFound` during these checks.
2. For a missing CRD, verify the `dacs` chart installed successfully with the
   required cluster-scoped permissions. Do not delete/recreate an existing CRD
   as a troubleshooting shortcut.
3. For a missing Cache, enable `cache.enabled` through the original Helm values
   file or create a Cache CR using the [deployment guide](../../user-guide.md).
4. Check `cache.name` and namespace overrides. The controller watches the Helm
   release namespace; keep the Cache CR in that namespace.
5. For an existing Cache without pods, inspect reconciliation errors and scaling
   configuration. Confirm the desired count is nonzero and the node selector
   matches eligible nodes. Restore
   [controller readiness](controller-startup.md) if needed.
6. If admission rejects Cache creation, use the
   [admission webhook TSG](admission-webhook.md).

## Verification Steps

```bash
kubectl get cache "$CACHE_NAME" -n "$NAMESPACE"
kubectl get statefulsets,pods -n "$NAMESPACE" \
  -l "dacs.azure.com/cache-name=$CACHE_NAME"
```

Confirm the Cache exists and the expected StatefulSet and pods appear. If pods
remain Pending, continue with the [scheduling TSG](cacheserver-scheduling.md).

## Contact Information

Use the channels in [SUPPORT.md](../../SUPPORT.md) and include redacted
diagnostics from the [escalation checklist](../installation-troubleshooting.md#escalation-checklist).
