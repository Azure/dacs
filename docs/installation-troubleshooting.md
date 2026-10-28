# DACS installation troubleshooting guides (TSGs)

Use these guides when a DACS Helm installation fails or the first cache does not
become usable. Each issue has a separate guide with Symptoms, Description,
Steps to Mitigate, Verification Steps, and Contact Information.

These guides cover the controller-based `dacs` chart used by the
[AKS quickstart](quickstart.md) and [deployment user guide](../user-guide.md).
The standalone `cache-server` chart has different values and resource names;
do not mix its `cacheServer.*` settings with the controller chart's `cache.*`
settings.

For issues after installation, see the
[post-installation troubleshooting guides](operations-troubleshooting.md).

## Installation issues

| Symptom | Troubleshooting guide |
| --- | --- |
| Wrong cluster, `Unauthorized`, or `Forbidden` | [Cluster access or permissions failure](tsg/cluster-access.md) |
| Chart pull fails or Helm times out | [Helm installation failure](tsg/helm-installation.md) |
| `ErrImagePull` or `ImagePullBackOff` | [Container image pull failure](tsg/image-pull.md) |
| Controller is not Ready | [Controller startup failure](tsg/controller-startup.md) |
| Cache resource is missing or no servers appear | [Cache creation failure](tsg/cache-creation.md) |
| Cache creation fails with a webhook error | [Admission webhook failure](tsg/admission-webhook.md) |
| CacheServer pods remain `Pending` | [CacheServer scheduling failure](tsg/cacheserver-scheduling.md) |
| Prereq init container fails or storage is missing | [NVMe prerequisite setup failure](tsg/nvme-setup.md) |
| CacheServer restarts or stays unready | [CacheServer startup failure](tsg/cacheserver-startup.md) |
| Pods are Ready but the client cannot connect | [Cache discovery or client connectivity failure](tsg/cache-connectivity.md) |

## Initial diagnostics

Examples assume namespace `dacs-system`, Helm release `dacs`, Cache
`cache-sample`, and the default `namePrefix=dacs`. Substitute your actual values
and resource names. Replace angle-bracket placeholders before running commands.
Individual guides include their own variable setup so they can be used directly.

```bash
export NAMESPACE="dacs-system"
export RELEASE="dacs"
export CACHE_NAME="cache-sample"

kubectl config current-context
kubectl cluster-info
helm list -n "$NAMESPACE" --all
helm status "$RELEASE" -n "$NAMESPACE"
kubectl get deployments,daemonsets,statefulsets,pods,services -n "$NAMESPACE" -o wide
kubectl get cache -n "$NAMESPACE"
kubectl get events -n "$NAMESPACE" --sort-by=.metadata.creationTimestamp
```

If a resource does not exist yet, its `NotFound` error identifies the stage to
investigate. If `kubectl` cannot reach the cluster, fix cluster access first.

## Safety precautions

- Record events and logs before restarting pods or retrying Helm.
- Keep the original values file and chart version. Do not retry with defaults
  that silently change storage mode, node selection, or sizing.
- Do not delete the namespace, Cache CRs, CRDs, or storage as a diagnostic step.
  Replacing an ephemeral cache pod loses its cached data.
- NVMe setup can format unused disks. Confirm node and disk ownership before
  installing or rerunning the prerequisite chart. Do not use `mkfs`, `wipefs`,
  or force-create a RAID array as a troubleshooting shortcut.

## Escalation checklist

If the issue persists, collect:

- Installation command, timestamp/time zone, chart version and `appVersion`,
  and relevant values overrides.
- Cluster/Kubernetes version, node OS/architecture/VM size, namespace, release,
  Cache name, and storage mode.
- Helm status/history, pod inventory, affected pod descriptions and events.
- Current and previous logs for the affected controller, CacheServer, or prereq
  init container.
- Cache CR specification/status and, for storage failures, read-only host
  diagnostics from the [NVMe setup guide](tsg/nvme-setup.md).
- Expected versus actual behavior and the first failing verification step.

Review and redact diagnostics before sharing. Do not include credentials,
kubeconfig files, tokens, Kubernetes Secret contents, or TLS private keys.
Logs, values, CRs, and host output may also contain sensitive storage URLs and
private endpoints.

Use the channels in [SUPPORT.md](../SUPPORT.md). Report suspected vulnerabilities
through [SECURITY.md](../SECURITY.md), not a public issue.
