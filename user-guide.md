# User Guide

This guide provides step-by-step instructions for setting up and using the
DACS Cache Server, including installing Helm, configuring NVMe storage,
and deploying the cache server cluster.

## Prerequisites

Before proceeding, ensure you have the following installed:

- Kubernetes cluster (v1.26+)
- Kubectl (v1.26+)
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

The cache server components require specific node labels for scheduling:

| Component | Label | Value | Purpose |
|-----------|-------|-------|---------|
| Cache Server Pods | `dacs.azure.com/processing-unit` | `cpu` | Targets NVMe-capable worker nodes |
| Scheduler Pod | `dacs.azure.com/processing-unit` | `system` | Targets system/control plane nodes |

**Label your NVMe worker nodes:**

```sh
# Label nodes for cache server pods (NVMe storage nodes)
kubectl label nodes <node-name> dacs.azure.com/processing-unit=cpu
```

**Label your system nodes (for the scheduler):**

```sh
# Label a system nodes for the scheduler pod
kubectl label nodes <node-name> dacs.azure.com/processing-unit=system
```

**Verify labels:**

```sh
kubectl get nodes --show-labels | grep -E 'dacs.azure.com|dacs.azure.com'
```

You can customize the node selector during installation. See [Node Selection](#node-selection) for details.

### Container Registry Access

The helm charts and container images are hosted in Azure Container Registry (ACR).
You need to configure your cluster to pull images from the registry.

**Attach ACR to AKS cluster**

```sh
# Attach ACR to your AKS cluster
az aks update \
  --resource-group <your-resource-group> \
  --name <your-aks-cluster> \
  --attach-acr dacs
```


## Installing Helm

To install Helm, please follow the official [Helm installation guide](https://helm.sh/docs/intro/install/).

## Installing cache-server

### Step 1: Login to Azure Container Registry

```sh
az acr login --name dacs
```

### Step 2: Install NVMe RAID Prerequisites (Required)

The cache server requires local NVMe storage to be configured as a RAID array.
Install the prerequisite chart to automatically set up RAID on each node:

```sh
helm install dacs-prereq \
  oci://dacs.azurecr.io/acstor/charts/dacs-prereq \
  --version <version> \
  --namespace cache-server \
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

Helm chart values are documented in: [dacs-prereq README](../helm/dacs-prereq/README.md).

### Step 3: Install Cache Server

```sh
helm install cache-server \
  oci://dacs.azurecr.io/acstor/charts/cache-server \
  --version <version> \
  --namespace cache-server \
  --wait --timeout 10m
```

Only one instance of cache-server can be installed per cluster.

Helm chart values are documented in: [cache-server README](../helm/cache-server/README.md).

### Verify Installation

```sh
kubectl get pods -n cache-server
```

Expected output:

```
NAME                                    READY   STATUS    RESTARTS   AGE
cache-server-0                          1/1     Running   0          2m
cache-server-1                          1/1     Running   0          2m
cache-server-2                          1/1     Running   0          2m
cacheserver-scheduler-xxxxxxx-xxxxx     1/1     Running   0          2m
```

## Configuration

### Basic Configuration

The most common configuration options:

```sh
helm install cache-server \
  oci://dacs.azurecr.io/acstor/charts/cache-server \
  --version <release> \
  --namespace cache-server \
  --set cacheServer.numServers=5 \
  --set cacheServer.serverSizeInGB=100 \
  --set cacheServer.serverThreads=160 \
  --set cacheServer.cpuLimit=8 \
  --set cacheServer.memoryLimitMB=8192
```

For a complete list of configuration options, see the [cache-server README](../helm/cache-server/README.md) and [values.yaml](../helm/cache-server/values.yaml).

### Using a Values File

For complex configurations, create a `values.yaml` file:

```yaml
cacheServer:
  numServers: 5
  serverSizeInGB: 100
  serverThreads: 160
  cpuLimit: 8
  memoryLimitMB: 8192
  evictionEnabled: true
  evictionSizeThreshold: 85
  evictionSizeTarget: 75
  evictionLastAccessThreshold: 7d
```

Install with the values file:

```sh
helm install cache-server \
  oci://dacs.azurecr.io/acstor/charts/cache-server \
  --version <release> \
  --namespace cache-server \
  -f values.yaml
```

## Monitoring

### View Logs

```sh
# All cache servers
kubectl logs -n cache-server -l app=cacheserver --prefix --all-containers

# Scheduler
kubectl logs -n cache-server -l app=cacheserver-scheduler --prefix --all-containers

# Events
kubectl events -n cache-server
```

## Troubleshooting

### Pods Stuck in Pending

Check pod events and node labels:

```sh
kubectl describe pod cache-server-0 -n cache-server
kubectl get nodes --show-labels | grep dacs.azure.com/processing-unit
```

### NVMe RAID Setup Fails

Check the prereq logs:

```sh
kubectl logs -n cache-server -l app.kubernetes.io/name=dacs-prereq -c nvme-raid-setup
```

Common causes:
- VM size doesn't have NVMe disks
- Node doesn't match the prereq chart's nodeSelector

### Cache Server CrashLoopBackOff

Check logs:

```sh
kubectl logs cache-server-0 -n cache-server --previous
```

## Uninstalling cache-server

To uninstall cache-server, follow these steps:

1. Uninstall the cache server:

   ```sh
   helm uninstall cache-server -n cache-server
   ```

2. Uninstall the prerequisites (optional):

   ```sh
   helm uninstall dacs-prereq -n cache-server
   ```

   > [!NOTE]
   > Uninstalling the prereq chart does **not** remove the RAID array or data.
   > The array persists on the node until manually removed.

3. Delete the namespace (optional):

   ```sh
   kubectl delete namespace cache-server
   ```
