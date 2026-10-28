# DACS Helm Installation Failure

## Symptoms

Helm reports OCI download errors, chart version not found, release ownership
conflicts, `context deadline exceeded`, or `timed out waiting for the condition`.

## Description

Helm must retrieve the requested chart and create its Kubernetes resources.
Download, authorization, ownership, or workload-readiness failures can prevent
installation. Chart `version` and image `appVersion` are different identifiers.
Helm readiness does not establish readiness of controller-created CacheServers.

## Steps to Mitigate

1. Inspect the chart and release. Replace `<chart-version>` with the pinned
   version used for your installation:

   ```bash
   export NAMESPACE="dacs-system"
   export RELEASE="dacs"
   export CHART_REF="oci://dacstest.azurecr.io/dacs/helmchart/dacs"
   helm version --short
   helm show chart "$CHART_REF" --version <chart-version>
   helm list -n "$NAMESPACE" --all
   helm history "$RELEASE" -n "$NAMESPACE"
   helm get values "$RELEASE" -n "$NAMESPACE"
   kubectl get events -n "$NAMESPACE" --sort-by=.metadata.creationTimestamp
   ```

   Release queries may return `NotFound` if installation never created a release.
2. For download errors, check DNS, proxy, firewall, and HTTPS access to the
   registry. Published artifacts at `dacstest.azurecr.io` support anonymous
   pulls; private mirrors may require authentication.
3. Confirm the exact chart version exists. Use its `appVersion` for matching
   image tags, rather than using the chart version as an image tag.
4. For an existing release, use `helm upgrade --install` with its original values
   file and pinned version. For ownership conflicts, inspect the resource's
   owning release; do not delete resources or strip ownership metadata blindly.
5. For timeouts, identify the failing pod using events and the
   [installation TSG index](../installation-troubleshooting.md). Increase the
   timeout only when the deployment is making progress.
6. With `--atomic`, a failed install is removed and a failed upgrade is rolled
   back. Capture diagnostics while the operation is pending where possible;
   resources may already be gone afterward.

## Verification Steps

```bash
helm status "$RELEASE" -n "$NAMESPACE"
kubectl rollout status deployment/dacs-manager -n "$NAMESPACE" --timeout=5m
```

Use your actual Deployment name if `namePrefix` is not `dacs`. Confirm release
status is `deployed`, then verify CacheServer readiness and run the
[quickstart smoke test](../quickstart.md#run-the-end-to-end-smoke-test).

## Contact Information

Use the channels in [SUPPORT.md](../../SUPPORT.md) and include redacted
diagnostics from the [escalation checklist](../installation-troubleshooting.md#escalation-checklist).
