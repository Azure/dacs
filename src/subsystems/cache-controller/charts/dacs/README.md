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
  --set cache.enable=true
```

## Configuration

### Controller Configuration

| Parameter | Description | Default |
|-----------|-------------|---------|
| `manager.replicas` | Number of controller replicas. Leader election is enabled automatically when `> 1`. | `1` |
| `manager.image.repository` | Controller image repository | `dacs/dacs` |
| `manager.image.tag` | Controller image tag | `latest` |
| `manager.image.pullPolicy` | Image pull policy | `IfNotPresent` |
| `manager.logLevel` | Controller log level via env (`CACHE_CONTROLLER_LOGLEVEL`) | `info` |
| `manager.resources.limits.cpu` | CPU limit | `500m` |
| `manager.resources.limits.memory` | Memory limit | `128Mi` |
| `manager.resources.requests.cpu` | CPU request | `10m` |
| `manager.resources.requests.memory` | Memory request | `64Mi` |
| `manager.priorityClassName` | PriorityClass for the controller pod. Empty uses the chart-managed `<fullname>-priority` PriorityClass (value `100000`); set a name to use an existing PriorityClass instead. | `""` |

### Cache Configuration

| Parameter | Description | Default |
|-----------|-------------|---------|
| `cache.enabled` | Deploy a Cache CR with controller | `false` |
| `cache.name` | Name of the Cache CR (max 53 chars, RFC 1123 DNS label) | `cache-sample` |
| `cache.namespace` | Namespace for Cache CR | `cache-server` |
| `cache.serverImage.repository` | Cache server image repository |  `cache-server` |
| `cache.serverImage.tag` | Cache server image tag | `latest` |
| `cache.scaling.static.numServers` | Number of cache servers | |
| `cache.scaling.dynamic.percentageOfNodesToUse` | Percentage of nodes to run cache on (1-100) | |
| `cache.scaling.dynamic.minServers` | Minimum cache servers, 1-499 (desired below this floors to 0) | |
| `cache.scaling.dynamic.maxServers` | Maximum cache servers, 1-499 (hard cap; must be >= minServers) | |
| `cache.serverSizeInGB` | Storage per server (GB) | `10` |
| `cache.serverThreads` | Threads per server | `16` |

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

```bash
helm upgrade dacs ./charts/dacs \
  --namespace dacs-system \
  --set manager.image.tag=local
```

**Note:** The controller pod will be recreated during upgrade. Existing Cache CRs are not affected.

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

**Common cause:** Cache server pods with persistent storage (`storage.ephemeral=false`) require a custom scheduler (`cacheserver-scheduler`) which must be deployed separately.

Check which scheduler the pod is using (pod names are `<cache-name>-<ordinal>`):
```bash
kubectl get pod <cache-name>-0 -n cache-server -o jsonpath='{.spec.schedulerName}'
```

**Solutions:**
- Use ephemeral storage: `--set cache.storage.ephemeral=true` (uses default scheduler)
- Or deploy the `cacheserver-scheduler` before installing the chart

Also check node selector:
```bash
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
