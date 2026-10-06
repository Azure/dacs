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
  --namespace dacs-system \
  --create-namespace \
  --values ./charts/dacs/values.yaml 
```

### Install Controller + Sample Cache

```bash
helm install dacs ./charts/dacs \
  --namespace dacs-system \
  --create-namespace \
  --values ./charts/dacs/values.yaml \
  --set cache.enabled=true
```

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

### Cache Configuration

| Parameter | Description | Default |
|-----------|-------------|---------|
| `cache.enabled` | Deploy a Cache CR with controller | `false` |
| `cache.name` | Name of the Cache CR (max 53 chars, RFC 1123 DNS label) | `cache-sample` |
| `cache.namespace` | Namespace for Cache CR; empty uses the release namespace | `""` |
| `cache.scaling.static.numServers` | Number of cache servers | |
| `cache.scaling.dynamic.percentageOfNodesToUse` | Percentage of nodes to run cache on (1-100) | |
| `cache.scaling.dynamic.minServers` | Minimum cache servers, 1-499 (desired below this floors to 0) | |
| `cache.scaling.dynamic.maxServers` | Maximum cache servers, 1-499 (hard cap; must be >= minServers) | |
| `cache.serverSizeInGB` | Storage per server (GB), minimum 1 | `4` |
| `cache.serverThreads` | Threads per server, range 4-128 | `80` |
| `cache.memoryLimitMB` | Optional memory limit per server, minimum 1024 MB | |
| `cache.tolerations` | CacheServer pod tolerations in the chart-managed Cache CR (`cache.enabled=true`) | `[]` |

### CacheServer Scheduling on Tainted Nodes

`manager.tolerations` applies only to the controller Deployment. CacheServer
StatefulSets are generated from each `Cache` resource, not from the controller's
pod settings. For a chart-managed Cache, set:

```yaml
cache:
  enabled: true
  tolerations:
    - key: sku
      operator: Equal
      value: gpu
      effect: NoSchedule
```

For a Cache managed separately from Helm, set the same list directly under
`spec.tolerations`:

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

### RBAC Configuration

| Parameter | Description | Default |
|-----------|-------------|---------|
| `rbacHelpers.enable` | Install admin/editor/viewer roles | `false` |

### CRD Configuration

CRDs are always installed (required for controller to function).

| Parameter | Description | Default |
|-----------|-------------|---------|
| `crd.keep` | Keep CRDs on uninstall | `true` |

**Warning:** Setting `crd.keep=false` will delete all Cache custom resources when uninstalling the chart.

### Monitoring Configuration

| Parameter | Description | Default |
|-----------|-------------|---------|
| `metrics.enable` | Enable metrics endpoint | `true` |
| `prometheus.enable` | Install ServiceMonitor | `false` |

## Upgrade

Helm does not upgrade existing CRDs. Apply the target chart's CRDs first.

```bash
set -euo pipefail
kubectl apply -f ./charts/dacs/crds
kubectl wait --for=condition=Established --timeout=60s -f ./charts/dacs/crds

helm upgrade dacs ./charts/dacs \
  --namespace dacs-system \
  --set manager.image.tag=local
```

**Note:** The controller pod will be recreated during upgrade. Existing Cache CRs are not affected.
Rollback retains the updated CRDs; verify compatibility with the previous controller.

## Uninstall

```bash
# Remove controller (CRDs are kept by default)
helm uninstall dacs -n dacs-system

# Remove CRDs manually if needed
kubectl delete crd caches.storage.azure.com
```

## Namespace Architecture

**Recommended:** Deploy controller and cache in separate namespaces:
- Controller: `dacs-system` (watches cluster-wide)
- Cache instances: `cache-server`, `app-namespace`, etc.

**Alternative:** Deploy both in the same namespace by setting `cache.namespace=""`.

## Troubleshooting

### Controller Not Starting

```bash
kubectl logs -n dacs-system -l app.kubernetes.io/name=dacs -f
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
helm get manifest dacs -n dacs-system | grep CustomResourceDefinition
```

## Development

### Testing

Run the built-in Helm tests to verify the deployment:

```bash
helm test dacs -n dacs-system
```

This runs three tests:
1. **Controller Test** - Verifies the controller pod is running
2. **CRD Test** - Verifies the Cache CRD is installed
3. **Cache Test** - Verifies Cache CR and pods (if `cache.enabled=true`)

View test logs:
```bash
kubectl logs -n dacs-system dacs-test-controller
kubectl logs -n dacs-system dacs-test-crd
kubectl logs -n dacs-system dacs-test-cache  # if cache enabled
```

### Linting

```bash
helm lint ./charts/dacs
```

### Template Rendering

```bash
helm template test ./charts/dacs \
  --set cache.enabled=true \
  --debug
```

### Schema Validation

The chart includes `values.schema.json` for automatic validation during install/upgrade.
