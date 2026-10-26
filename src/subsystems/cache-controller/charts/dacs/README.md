# DACS-cache Helm Chart

A Kubernetes controller that manages distributed cache infrastructure through custom resources.

## Prerequisites

- Kubernetes 1.19+
- Helm 3.8+
- CRD support enabled in the cluster

## Quick Start

### Install Controller Only

```bash
helm install dacs ./charts/dacs \
  --namespace cache-server \
  --create-namespace \
  --values ./charts/dacs/values.yaml 
```

### Install Controller and Example Cache

```bash
helm install dacs ./charts/dacs \
  --namespace cache-server \
  --create-namespace \
  --values ./charts/dacs/values.yaml
```

After the controller chart succeeds, apply one of the example Cache manifests:

```bash
kubectl apply \
  --namespace cache-server \
  -f ./charts/dacs/examples/cache/static.yaml
```

Use [examples/cache/dynamic.yaml](examples/cache/dynamic.yaml) instead to enable
dynamic scaling. `make deploy CONTROLLER=new` installs the controller and
prerequisites only; apply the desired Cache manifest separately. AKS test
pipelines use these same examples, with the static manifest also used for
performance tests.

## Configuration

### Controller Configuration

| Parameter | Description | Default |
|-----------|-------------|---------|
| `global.registry` | Registry used for DACS images | `dacstest.azurecr.io` |
| `manager.replicas` | Number of controller replicas. Leader election is enabled automatically when `> 1`. | `1` |
| `manager.image.repository` | Controller image repository | `dacs/cache-controller` |
| `manager.image.tag` | Controller image tag; empty uses the chart app version | `""` |
| `manager.image.pullPolicy` | Image pull policy | `IfNotPresent` |
| `manager.cacheServerImage.repository` | Cache server image repository | `dacs/cache-server` |
| `manager.cacheServerImage.tag` | Cache server image tag; empty uses the chart app version | `""` |
| `manager.logLevel` | Controller log level via env (`CACHE_CONTROLLER_LOGLEVEL`) | `info` |
| `manager.tolerations` | Controller pod tolerations only; does not affect CacheServer pods | `[{key: CriticalAddonsOnly, operator: Exists}]` |
| `manager.resources.limits.cpu` | CPU limit | `500m` |
| `manager.resources.limits.memory` | Memory limit | `512Mi` |
| `manager.resources.requests.cpu` | CPU request | `10m` |
| `manager.resources.requests.memory` | Memory request | `64Mi` |
| `manager.priorityClassName` | PriorityClass for the controller pod. Empty uses the chart-managed `<fullname>-priority` PriorityClass (value `100000`); set a name to use an existing PriorityClass instead. | `""` |

### Example Cache Configuration

The controller chart does not create Cache resources. Start with the static,
dynamic, or Minikube manifests in [`examples`](examples/README.md).

### CacheServer Scheduling on Tainted Nodes

`manager.tolerations` applies only to the controller Deployment. CacheServer
StatefulSets are generated from each `Cache` resource, not from the controller's
pod settings. Set CacheServer tolerations directly under `spec.tolerations`:

```yaml
apiVersion: storage.azure.com/v1
kind: Cache
metadata:
  name: gpu-node-cache
spec:
  scaling:
    static:
      numServers: 2
  serverSizeInGB: 4
  serverThreads: 8
  nodeSelectorKey: agentpool
  nodeSelectorValue: gpu
  tolerations:
    - key: sku
      operator: Equal
      value: gpu
      effect: NoSchedule
```

Choose node selector labels that exist on your target nodes. Tolerations permit
placement on matching tainted nodes; they neither select nodes nor request GPU
resources. The example tolerates `sku=gpu:NoSchedule` without allocating a GPU.

Standard Kubernetes toleration fields are supported: `key`, `operator`
(`Equal` by default, or `Exists`), `value`, `effect`, and `tolerationSeconds`.
An empty key requires `Exists`, which requires an empty value. An empty effect
matches all effects; `tolerationSeconds` is only valid with `NoExecute`.
Omitted or empty tolerations add no CacheServer tolerations.

Update the Cache resource rather than patching generated pods or StatefulSets.
Changes update the pod template using the existing StatefulSet rolling-update
strategy. The controller restores configured tolerations on reconciliation,
restart, and StatefulSet recreation; replacement pods inherit the template.
Setting `spec.tolerations: []` removes them from the desired template.

When upgrading an existing installation, install the updated Cache CRD before
using this field. Helm does not upgrade CRDs in the chart's `crds/` directory
automatically; follow the [upgrade procedure](#upgrade) using
`charts/dacs/crds/storage.azure.com_caches.yaml`.

Query the installed CRD for fields omitted from the examples, including
dynamic scaling bounds, ephemeral storage, InfiniBand, and image pull policy:

```bash
kubectl explain cache.spec --api-version=storage.azure.com/v1 --recursive
kubectl explain cache.spec.scaling.dynamic --api-version=storage.azure.com/v1
```

### RBAC Configuration

| Parameter | Description | Default |
|-----------|-------------|---------|
| `rbacHelpers.enable` | Install optional admin/editor roles | `false` |

The Cache viewer role is always installed. Its read permissions are aggregated
into the standard Kubernetes `view` role.

### CRD Configuration

CRDs are always installed (required for controller to function).

| Parameter | Description | Default |
|-----------|-------------|---------|
| `crd.keep` | Keep CRDs on uninstall | `true` |

Setting `crd.keep=false` removes the CRD on uninstall. It does not bypass the
Cache resource guard described in [Uninstall](#uninstall).

### Monitoring Configuration

| Parameter | Description | Default |
|-----------|-------------|---------|
| `metrics.enable` | Enable metrics endpoint | `true` |
| `prometheus.enable` | Install ServiceMonitor | `false` |

## Upgrade

**Before upgrading from a chart that created a Cache:** inspect the existing
release with `helm get values dacs -n cache-server --all`
and `helm get manifest dacs -n cache-server`. Removing the
Cache template can delete a previously chart-owned Cache during upgrade.
Do not upgrade such a release until its Cache ownership has been migrated
and the migration verified. Merely removing `cache.enabled` or setting it
to `false` does not protect an existing chart-owned resource.

Legacy `cache.enabled=true` fails rendering with a migration message.
`cache.enabled=false` is accepted for controller-only consumers; other
legacy `cache.*` settings are ignored because Cache configuration belongs in
standalone manifests. This values guard is not a substitute for inspecting
existing release resources, since old values may not be passed to the new chart.

Helm does not upgrade existing CRDs. Apply the target chart's CRDs first.

```bash
set -euo pipefail
kubectl apply -f ./charts/dacs/crds
kubectl wait --for=condition=Established --timeout=60s -f ./charts/dacs/crds

helm upgrade dacs ./charts/dacs \
  --namespace cache-server \
  --set manager.image.tag=local
```

**Note:** The controller pod will be recreated during upgrade. Existing Cache CRs are not affected.
Rollback retains the updated CRDs; verify compatibility with the previous controller.

Controller chart upgrades do not manage separately applied Cache manifests.
Edit and reapply the selected example when its Cache configuration changes:

```bash
kubectl apply \
  --namespace cache-server \
  -f src/subsystems/cache-controller/charts/dacs/examples/cache/static.yaml
```

## Uninstall

Uninstalling the controller chart runs a pre-delete hook that blocks uninstall
while any Cache resources remain in the cluster, including resources that are
still terminating. Delete the Cache resources you own and wait for cleanup
before uninstalling the controller. For example:

```bash
kubectl delete -n cache-server \
  -f ./charts/dacs/examples/cache/static.yaml \
  --wait=true --timeout=5m
helm uninstall dacs -n cache-server
```

| Parameter | Description | Default |
|-----------|-------------|---------|
| `cleanup.forceDeleteCaches` | Delete all Cache resources cluster-wide; after the cleanup timeout, clear finalizers on remaining terminating Cache resources. | `false` |
| `cleanup.timeoutSeconds` | Time allowed for normal cleanup before force mode removes remaining Cache finalizers. | `300` |

To explicitly allow deletion of **all Cache resources in the cluster**, save
`cleanup.forceDeleteCaches=true` on the release **before the first uninstall
attempt**. Helm uninstall does not accept `--set`; use the same chart version
and existing release configuration for the upgrade:

```bash
helm upgrade dacs ./charts/dacs \
  -n cache-server --reuse-values \
  --set cleanup.forceDeleteCaches=true
helm uninstall dacs -n cache-server --timeout=10m
```

This opt-in includes Cache resources created by other users or releases, even
when `crd.keep=true`. It first requests deletion without waiting, then polls
for completion. After `cleanup.timeoutSeconds`, it clears **all finalizers**
on remaining terminating Cache resources, including finalizers added by other
controllers. Uninstall proceeds only after no Cache resources remain.

**Warning:** Removing finalizers bypasses unfinished cleanup and can leave
resources behind. Use force deletion only when this is acceptable. It does
not remove finalizers from child resources or erase cached files on nodes.
The value remains enabled on the release until changed or the release is
uninstalled.

If uninstall has already been blocked, delete the remaining Cache resources,
wait for their cleanup, and retry `helm uninstall`. A failed uninstall hook
can leave the release in `uninstalling` status, preventing a Helm upgrade to
enable force deletion.

Permission or API errors fail the hook. Do not use `helm uninstall --no-hooks`
to bypass this protection: it can remove the controller while Cache resources
still depend on it. Do not uninstall if those resources must remain.

## Namespace Architecture

Install the controller and Cache resources in the same namespace. These
instructions use `cache-server`. If you install the Helm release in another
namespace, use that namespace for the Cache examples and subsequent commands.
The example printed by Helm uses the release namespace.

## Troubleshooting

### Controller Not Starting

```bash
kubectl logs -n cache-server -l app.kubernetes.io/name=dacs -f
```

### Cache Pods Pending

Persistent-storage scheduling is integrated into the controller manager; do
not deploy a separate scheduler. Persistent cache pods use
`schedulerName: cache-controller`.

Check which scheduler the pod is using (pod names are `<cache-name>-<ordinal>`):
```bash
CACHE_NAME=cache-sample
kubectl get pod "$CACHE_NAME-0" -n cache-server -o jsonpath='{.spec.schedulerName}'
```

If a pod remains pending, inspect its scheduling events and verify that the
configured host path and node selection match available nodes:

```bash
kubectl describe pod "$CACHE_NAME-0" -n cache-server
kubectl describe cache -n cache-server
kubectl get nodes --show-labels
```

### CRD Not Found

```bash
kubectl get crd caches.storage.azure.com
# If missing, CRDs should be installed automatically. Check:
helm get manifest dacs -n cache-server | grep CustomResourceDefinition
```

## Development

### Testing

With the controller installed, validate both example manifests against the
cluster without creating resources:

```bash
kubectl apply --dry-run=server \
  --namespace cache-server \
  -f ./charts/dacs/examples/cache/static.yaml
kubectl apply --dry-run=server \
  --namespace cache-server \
  -f ./charts/dacs/examples/cache/dynamic.yaml
```

### Linting

```bash
helm lint ./charts/dacs
```

### Template Rendering

```bash
helm template test ./charts/dacs --debug

kubectl apply --dry-run=server \
  --namespace cache-server \
  -f ./charts/dacs/examples/cache/static.yaml
```

### Schema Validation

The chart includes `values.schema.json` for automatic validation during install/upgrade.
