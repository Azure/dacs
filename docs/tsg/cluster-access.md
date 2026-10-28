# DACS Cluster Access or Permissions Failure

## Symptoms

Installation commands target the wrong cluster or fail with connection timeouts,
`Unauthorized`, or `Forbidden`.

## Description

Helm and `kubectl` require connectivity and credentials for the intended
Kubernetes cluster. The DACS chart also installs cluster-scoped CRDs, RBAC, and
webhook configurations, so namespace-only permissions may not be sufficient.

## Steps to Mitigate

1. Set the intended namespace and inspect the active context:

   ```bash
   export NAMESPACE="dacs-system"
   kubectl config current-context
   kubectl cluster-info
   kubectl get nodes
   ```

2. Select the intended kubeconfig context. For AKS, refresh credentials for the
   intended subscription, resource group, and cluster through your normal Azure
   CLI access workflow.
3. For connection failures, check VPN/private-cluster connectivity and Kubernetes
   API DNS resolution.
4. For authorization failures, inspect the denied operation and ask the cluster
   administrator to grant the required permissions. These checks are examples,
   not an exhaustive authorization check:

   ```bash
   kubectl auth can-i create deployments -n "$NAMESPACE"
   kubectl auth can-i create customresourcedefinitions.apiextensions.k8s.io
   kubectl auth can-i create clusterroles.rbac.authorization.k8s.io
   ```

## Verification Steps

Confirm that the active context is the intended cluster, cluster queries
succeed, and the installer is authorized for the previously denied operation.
Retry the original installation command with its original version and values.

## Contact Information

Contact the cluster administrator for connectivity or permission changes.
For DACS help, use the channels in [SUPPORT.md](../../SUPPORT.md) and include
redacted diagnostics from the [escalation checklist](../installation-troubleshooting.md#escalation-checklist).
