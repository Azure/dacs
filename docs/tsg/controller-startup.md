# DACS Controller Startup Failure

## Symptoms

The controller Deployment does not become Available. Its pod remains Pending,
restarts, reports `CrashLoopBackOff` or `OOMKilled`, or fails readiness probes.

## Description

The controller reconciles Cache resources and manages cache infrastructure.
Scheduling, resource limits, RBAC, invalid configuration, or certificate startup
errors can prevent it from becoming Ready.

## Steps to Mitigate

1. Inspect the Deployment, pod events, and logs. These examples assume
   `namePrefix=dacs`; substitute your actual Deployment name if different.
   Replace `<controller-pod>` with the pod returned by the query:

   ```bash
   export NAMESPACE="dacs-system"
   kubectl describe deployment dacs-manager -n "$NAMESPACE"
   kubectl get pods -n "$NAMESPACE" -l control-plane=controller-manager
   kubectl logs deployment/dacs-manager -n "$NAMESPACE" -c manager --tail=200
   kubectl describe pod <controller-pod> -n "$NAMESPACE"
   ```

2. If the controller container restarted, capture its previous logs before
   replacing the pod:

   ```bash
   kubectl logs <controller-pod> -n "$NAMESPACE" -c manager --previous --tail=200
   ```

3. For Pending pods, inspect affinity, selectors, taints, and available resources.
   Defaults target Linux `amd64`/`arm64` nodes.
4. For `OOMKilled`, review memory usage and the manager limit; adjust
   `manager.resources` through your Helm values if needed.
5. For `Forbidden`, review the denied operation and chart-installed
   ServiceAccount, RoleBindings, and ClusterRoleBindings with the administrator.
6. For certificate or webhook errors, follow the
   [admission webhook TSG](admission-webhook.md). For invalid flags, correct
   image/version mismatches or custom `manager.extraArgs` / environment overrides.

## Verification Steps

```bash
kubectl rollout status deployment/dacs-manager -n "$NAMESPACE" --timeout=5m
kubectl get pods -n "$NAMESPACE" -l control-plane=controller-manager
```

Confirm the Deployment is Available, its expected replicas are Ready, and restart
counts do not continue increasing.

## Contact Information

Use the channels in [SUPPORT.md](../../SUPPORT.md) and include redacted
diagnostics from the [escalation checklist](../installation-troubleshooting.md#escalation-checklist).
