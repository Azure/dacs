# DACS deployment and operations guide

Use this guide to install and operate a production DACS deployment with local
NVMe/RAID storage. It covers prerequisites, installation, configuration,
verification, scaling, troubleshooting, and uninstall.

For a first deployment without NVMe or RAID provisioning, use the
[10-minute AKS quickstart](docs/quickstart.md), which creates a single
ephemeral CacheServer and runs an end-to-end smoke test.
For the control-plane and data-plane request flow, see
[Architecture](docs/architecture.md).

## Production prerequisites

Before proceeding, ensure you have the following installed:

- Kubernetes cluster (v1.26+)
- `kubectl` (v1.26+)
- Helm (v3.16+)
- Azure CLI (for AKS deployments)

### Node Requirements

The cache server requires nodes with local NVMe storage. Example Azure VM sizes:

| VM Size | NVMe Disks | Total NVMe Storage |
|---------|------------|-------------------|
| `Standard_L8s_v3` | 1 | 1.92 TB |
| `Standard_L16s_v3` | 2 | 3.84 TB |
| `Standard_L32s_v3` | 4 | 7.68 TB |

## Deploying an AKS Cluster

If you need to deploy a new AKS cluster with NVMe-capable nodes, see the [Deployment README](deploy/README.md) for detailed instructions.

### Node Labels

The cache server pods require a node label for scheduling:

| Component | Label | Value | Purpose |
|-----------|-------|-------|---------|
| Cache Server Pods | `dacs.azure.com/processing-unit` | `cpu` | Targets NVMe-capable worker nodes |

**Label your NVMe worker nodes:**

```sh
# Label nodes for cache server pods (NVMe storage nodes)
kubectl label nodes <node-name> dacs.azure.com/processing-unit=cpu
```

**Verify labels:**

```sh
kubectl get nodes --show-labels | grep dacs.azure.com/processing-unit
```

You can customize the node selector during installation by setting `cache.nodeSelectorKey` and `cache.nodeSelectorValue`.

### Published artifacts

The published DACS OCI charts and images at `dacs.azurecr.io` are publicly
readable. Helm and Kubernetes can pull them anonymously.

## Installing Helm

To install Helm, please follow the official [Helm installation guide](https://helm.sh/docs/intro/install/).

## Install DACS

Use the same release version for the prerequisite and DACS charts. The DACS
chart's `appVersion` identifies the matching image tag. If you override image
settings, keep `dacs.azurecr.io/cache-controller`,
`dacs.azurecr.io/cache-server`, and
`dacs.azurecr.io/cache-test-client` on that matching version.

### Step 1: Install NVMe RAID Prerequisites (Required)

The cache server requires local NVMe storage to be configured as a RAID array.
Install the prerequisite chart to automatically set up RAID on each node:

```sh
helm install dacs-prereq \
  oci://dacs.azurecr.io/acstor/charts/dacs-prereq \
  --version <version> \
  --namespace dacs-system \
  --create-namespace \
  --wait --timeout 10m
```

When installed, a DaemonSet will automatically:

- Detect unused NVMe devices on each node
- Create a RAID 0 array using **mdadm**
- Format with ext4 and mount to `/var/lib/ssd/cacheserver`
- Add fstab entry for persistence across reboots

> [!NOTE]
> The RAID setup is idempotent. If a RAID array already exists, the script
> will detect it and skip creation.

### Step 2: Install DACS Cache Controller

The `dacs` chart deploys a Kubernetes controller that manages cache
infrastructure through a Cache custom resource (CR).

```sh
helm install dacs \
  oci://dacs.azurecr.io/acstor/charts/dacs \
  --version <version> \
  --namespace dacs-system \
  --create-namespace \
  --set manager.image.repository=dacs.azurecr.io/cache-controller \
  --set manager.cacheServerImage.repository=dacs.azurecr.io/cache-server \
  --wait --timeout 10m
```

### Step 3: Deploy a Cache Instance

Create a cache cluster by enabling the Cache CR:

```sh
helm upgrade dacs \
  oci://dacs.azurecr.io/acstor/charts/dacs \
  --version <version> \
  --namespace dacs-system \
  --set manager.image.repository=dacs.azurecr.io/cache-controller \
  --set manager.cacheServerImage.repository=dacs.azurecr.io/cache-server \
  --set cache.enabled=true \
  --set cache.scaling.static.numServers=3 \
  --set cache.serverSizeInGB=100
```

The controller will create and manage the cache server StatefulSet automatically.

Helm chart values are documented in the
[dacs README](src/subsystems/cache-controller/charts/dacs/README.md).

### Verify Installation

Check the controller is running:

```sh
kubectl get pods -n dacs-system
```

Expected output:

```
NAME                                          READY   STATUS    RESTARTS   AGE
dacs-manager-xxxxxxx-xxxxx           1/1     Running   0          2m
```

Check the Cache custom resource:

```sh
kubectl get cache -n dacs-system
```

Expected output:

```
NAME           AGE
cache-sample   2m
```

Check the discovery service. Cache clients use this service to discover
CacheServers and connect on port `9065`:

```sh
kubectl get service cache-sample-discovery -n dacs-system
```

Check the cache server pods (created by the controller). Pod names are
derived from the Cache CR name (default `cache-sample`):

```sh
# List pods for a specific Cache CR
kubectl get pods -n dacs-system -l dacs.azure.com/cache-name=cache-sample

# Or list all cache server pods across all Cache CRs
kubectl get pods -n dacs-system -l app=cacheserver
```

Expected output:

```
NAME              READY   STATUS    RESTARTS   AGE
cache-sample-0    1/1     Running   0          2m
cache-sample-1    1/1     Running   0          2m
cache-sample-2    1/1     Running   0          2m
```

## Configuration

### Basic Configuration

The most common configuration options:

```sh
helm upgrade dacs \
  oci://dacs.azurecr.io/acstor/charts/dacs \
  --version <version> \
  --namespace dacs-system \
  --set manager.image.repository=dacs.azurecr.io/cache-controller \
  --set manager.cacheServerImage.repository=dacs.azurecr.io/cache-server \
  --set cache.enabled=true \
  --set cache.scaling.static.numServers=5 \
  --set cache.serverSizeInGB=100 \
  --set cache.serverThreads=160 \
  --set cache.cpuLimit=8 \
  --set cache.memoryLimitMB=8192
```

For a complete list of configuration options, see the
[dacs README](src/subsystems/cache-controller/charts/dacs/README.md)
and [values.yaml](src/subsystems/cache-controller/charts/dacs/values.yaml).

### Using a Values File

For complex configurations, create a `values.yaml` file:

```yaml
manager:
  image:
    repository: dacs.azurecr.io/cache-controller
  cacheServerImage:
    repository: dacs.azurecr.io/cache-server

cache:
  enabled: true
  scaling:
    static:
      numServers: 5
  serverSizeInGB: 100
  serverThreads: 160
  cpuLimit: 8
  memoryLimitMB: 8192
  eviction:
    enableAccessTimeEviction: true
    evictionMaintainFreeSpace: true
    sizeThreshold: "90"
    sizeTarget: "80"
    lastAccessThreshold: "30d"
    passFrequency: "1h"
  storage:
    hostPath:
      nodeCacheFolderPath: /var/lib/ssd/cacheserver
```

Install with the values file:

```sh
helm upgrade --install dacs \
  oci://dacs.azurecr.io/acstor/charts/dacs \
  --version <version> \
  --namespace dacs-system \
  -f values.yaml
```

## Scale a cache

For a statically scaled cache, update `numServers` with the same chart version
used for the deployment:

```sh
helm upgrade dacs \
  oci://dacs.azurecr.io/acstor/charts/dacs \
  --version <version> \
  --namespace dacs-system \
  --reuse-values \
  --set cache.scaling.static.numServers=<server-count>
```

Verify the requested replicas and pod readiness:

```sh
kubectl get cache cache-sample -n dacs-system
kubectl get pods \
  -n dacs-system \
  --selector=dacs.azure.com/cache-name=cache-sample
```

For percentage-based dynamic scaling, server bounds, stabilization behavior,
and scaling status, see
[Cache controller scaling](src/subsystems/cache-controller/docs/controller/scaling.md).

## Monitoring

### View Logs

```sh
# Controller logs
kubectl logs -n dacs-system -l app.kubernetes.io/name=dacs --prefix -f

# Cache server logs (all cache pods)
kubectl logs -n dacs-system -l app=cacheserver --prefix --all-containers

# Cache server logs (specific Cache CR)
kubectl logs -n dacs-system -l dacs.azure.com/cache-name=cache-sample --prefix --all-containers

# Events
kubectl events -n dacs-system
```

### Check Cache Status

```sh
# View the Cache CR status
kubectl get cache -n dacs-system

# Detailed status
kubectl describe cache -n dacs-system
```

## Troubleshooting

### Controller Not Starting

Check controller logs:

```sh
kubectl logs -n dacs-system -l app.kubernetes.io/name=dacs -f
```

### Cache CR Not Reconciling

Check the Cache resource status:

```sh
kubectl describe cache -n dacs-system
```

Check controller logs for reconciliation errors:

```sh
kubectl logs -n dacs-system -l app.kubernetes.io/name=dacs -f
```

### Pods Stuck in Pending

Check pod events and node labels:

```sh
kubectl describe pod <cache-name>-0 -n dacs-system
kubectl get nodes --show-labels | grep dacs.azure.com/processing-unit
```

### NVMe RAID Setup Fails

Check the prereq logs:

```sh
kubectl logs -n dacs-system -l app.kubernetes.io/name=dacs-prereq -c nvme-raid-setup
```

Common causes:

- VM size doesn't have NVMe disks
- Node doesn't match the prereq chart's nodeSelector

### Cache Server CrashLoopBackOff

Check logs:

```sh
kubectl logs <cache-name>-0 -n dacs-system --previous
```

## Uninstall DACS

To uninstall DACS, follow these steps:

1. Delete Cache custom resources first:

   ```sh
   kubectl delete cache --all -n dacs-system
   ```

   > [!NOTE]
   > Deleting the Cache CR removes the cache server StatefulSet and pods.

2. Uninstall the DACS controller:

   ```sh
   helm uninstall dacs -n dacs-system
   ```

3. Uninstall the prerequisites (optional):

   ```sh
   helm uninstall dacs-prereq -n dacs-system
   ```

   > [!NOTE]
   > Uninstalling the prereq chart does **not** remove the RAID array or data.
   > The array persists on the node until manually removed.

4. Delete the namespace (optional):

   ```sh
   kubectl delete namespace dacs-system
   ```

## Development and contribution

To build DACS locally, see [Build from source](docs/build-from-source.md).
Before proposing a change, read [Contributing](CONTRIBUTING.md).
