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

The published DACS OCI charts and images at `dacstest.azurecr.io` are publicly
readable. Helm and Kubernetes can pull them anonymously.

## Installing Helm

To install Helm, please follow the official [Helm installation guide](https://helm.sh/docs/intro/install/).

## Install DACS

The DACS chart bundles optional NVMe prerequisites. Its `appVersion`
identifies the matching container-safe image tag. If you
override image settings, keep
`dacstest.azurecr.io/dacs/cache-controller`,
`dacstest.azurecr.io/dacs/cache-server`, and
`dacstest.azurecr.io/dacs/cache-test-client` on that matching version.

### Step 1: Install the Controller with NVMe Prerequisites

Enable the bundled dependency to prepare local NVMe storage:

```sh
helm install dacs \
  oci://dacstest.azurecr.io/dacs/helmchart/dacs \
  --version <version> \
  --namespace cache-server \
  --create-namespace \
  --set cacheServerPrereq.enabled=true \
  --atomic --timeout 15m
```

When installed, a DaemonSet will automatically:

- Detect unused NVMe devices on each node
- Create a RAID 0 array using **mdadm**
- Format with ext4 and mount to `/var/lib/ssd/cacheserver`
- Add fstab entry for persistence across reboots

> [!NOTE]
> The RAID setup is idempotent. If a RAID array already exists, the script
> will detect it and skip creation.
>
> The init container requests 512 MiB of memory and has a 2 GiB limit.
> Storage tools are preinstalled in the cache-setup image.

Wait for the release and its prerequisite rollout check to succeed before
creating a Cache. The hook does not block workloads created in the same
operation or stop existing Cache workloads during storage maintenance.

### Step 2: Deploy a Cache Instance

After the controller and prerequisites are ready, apply a standalone Cache:

```sh
kubectl apply --namespace cache-server \
  -f src/subsystems/cache-controller/charts/dacs/examples/cache/static.yaml
```

Use [dynamic.yaml](src/subsystems/cache-controller/charts/dacs/examples/cache/dynamic.yaml)
to scale with eligible nodes. Edit sizing, resources, and storage in the
manifest. The controller creates and manages the CacheServer StatefulSet.

Helm chart values are documented in the
[dacs README](src/subsystems/cache-controller/charts/dacs/README.md).

### Verify Installation

Check the controller is running:

```sh
kubectl get pods -n cache-server -l app.kubernetes.io/name=dacs
```

Expected output:

```
NAME                                          READY   STATUS    RESTARTS   AGE
dacs-manager-xxxxxxx-xxxxx           1/1     Running   0          2m
```

Check the Cache custom resource:

```sh
kubectl get cache -n cache-server
```

Expected output:

```
NAME           AGE
cache-sample   2m
```

Check the discovery service. Cache clients use this service to discover
CacheServers and connect on port `9065`:

```sh
kubectl get service cache-sample-discovery -n cache-server
```

Check the cache server pods (created by the controller). Pod names are
derived from the Cache CR name (default `cache-sample`):

```sh
# List pods for a specific Cache CR
kubectl get pods -n cache-server -l dacs.azure.com/cache-name=cache-sample

# Or list all cache server pods across all Cache CRs
kubectl get pods -n cache-server -l app=cacheserver
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

These commands update an existing release after storage setup succeeds.

```sh
helm upgrade dacs \
  oci://dacstest.azurecr.io/dacs/helmchart/dacs \
  --version <version> \
  --namespace cache-server \
  --reuse-values \
  --set manager.logLevel=debug
```

For controller options, see the
[dacs README](src/subsystems/cache-controller/charts/dacs/README.md).

### Using a Cache Manifest

Copy a manifest from
[`examples`](src/subsystems/cache-controller/charts/dacs/examples),
edit its fields, and reapply it independently of the controller Helm release:

```sh
kubectl apply --namespace cache-server -f my-cache.yaml
```

Before upgrading a release that used `cache.enabled=true`, read the
[migration warning](src/subsystems/cache-controller/charts/dacs/README.md#upgrade).
Removing old values alone does not preserve a chart-owned Cache.

## Scale a cache

For a statically scaled Cache, update `spec.scaling.static.numServers` in its
manifest and reapply it:

```sh
kubectl apply --namespace cache-server -f my-cache.yaml
```

Verify the requested replicas and pod readiness:

```sh
kubectl get cache cache-sample -n cache-server
kubectl get pods \
  -n cache-server \
  --selector=dacs.azure.com/cache-name=cache-sample
```

For percentage-based dynamic scaling, server bounds, stabilization behavior,
and scaling status, see
[Cache controller scaling](src/subsystems/cache-controller/docs/controller/scaling.md).

## Monitoring

### View Logs

```sh
# Controller logs
kubectl logs -n cache-server -l app.kubernetes.io/name=dacs --prefix -f

# Cache server logs (all cache pods)
kubectl logs -n cache-server -l app=cacheserver --prefix --all-containers

# Cache server logs (specific Cache CR)
kubectl logs -n cache-server -l dacs.azure.com/cache-name=cache-sample --prefix --all-containers

# Events
kubectl events -n cache-server
```

### Check Cache Status

```sh
# View the Cache CR status
kubectl get cache -n cache-server

# Detailed status
kubectl describe cache -n cache-server
```

## Troubleshooting

### Controller Not Starting

Check controller logs:

```sh
kubectl logs -n cache-server -l app.kubernetes.io/name=dacs -f
```

### Cache CR Not Reconciling

Check the Cache resource status:

```sh
kubectl describe cache -n cache-server
```

Check controller logs for reconciliation errors:

```sh
kubectl logs -n cache-server -l app.kubernetes.io/name=dacs -f
```

### Pods Stuck in Pending

Check pod events and node labels:

```sh
kubectl describe pod <cache-name>-0 -n cache-server
kubectl get nodes --show-labels | grep dacs.azure.com/processing-unit
```

### NVMe RAID Setup Fails

Check the prereq logs:

```sh
kubectl logs -n cache-server -l app.kubernetes.io/component=nvme-setup,app.kubernetes.io/instance=dacs -c nvme-raid-setup
```

Common causes:

- VM size doesn't have NVMe disks
- Node doesn't match the prereq chart's nodeSelector

### Cache Server CrashLoopBackOff

Check logs:

```sh
kubectl logs <cache-name>-0 -n cache-server --previous
```

## Uninstall DACS

To uninstall DACS, follow these steps:

1. Delete Cache custom resources first:

   ```sh
   kubectl delete cache --all -n cache-server
   ```

   > [!NOTE]
   > Deleting the Cache CR removes the cache server StatefulSet and pods.

2. Uninstall the DACS controller:

   ```sh
   helm uninstall dacs -n cache-server
   ```

   > [!NOTE]
   > Uninstalling the parent chart removes its prerequisite resources but does **not** remove the RAID array or data.
   > The array persists on the node until manually removed.

3. Delete the namespace (optional):

   ```sh
   kubectl delete namespace cache-server
   ```

## Development and contribution

To build DACS locally, see [Build from source](docs/build-from-source.md).
Before proposing a change, read [Contributing](CONTRIBUTING.md).
