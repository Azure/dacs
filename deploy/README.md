# Deployment

This document describes how to deploy clusters for cache-server testing.

## Quick Start

```bash
# Create a cluster
make provision TARGET=<aks|minikube>

# Install helm charts
make deploy

# Run e2e tests (auto-detects cluster type)
make test-e2e

# Clean up helm charts
make undeploy

# Delete cluster (auto-detects cluster type from kubectl context)
make unprovision
```

## Minikube (Local Development)

```bash
# Create minikube cluster
make provision TARGET=minikube

# Load images into minikube
make images load-images

# Install helm charts
make deploy

# Clean up
make undeploy
make unprovision
```

## AKS Cluster

### Parameters

| Parameter | Description | Default Value |
|-----------|-------------|---------------|
| `AKS_LOCATION` | The Azure region for the resources | `eastus` |
| `AKS_TEMPLATE` | The template to use (parameter file name) | `nvme` |
| `AKS_RESOURCE_GROUP` | The name of the resource group | `${USER}-cache-${AKS_TEMPLATE}-${AKS_LOCATION}` |
| `AKS_IS_TEST` | Mark cluster as test (for cleanup policies) | `false` |
| `AKS_PARAMETER_FILE` | Optional parameter file override for the AKS creation script | Named template file |
| `AKS_RESOURCE_PREFIX` | Integration-only suite/build/stage-attempt/job-attempt prefix; requires the existing `dacs-integration` RG and `AKS_IS_TEST=true` | Empty (local subscription deployment) |

### Personal cluster initialization

[dev-setup.sh](../dev-setup.sh) installs development tools, including Azure CLI;
it does not create Azure resources or configure pipeline identities. After
installing the tools, use your own Azure login, subscription, and registry:

```bash
az login
az account set --subscription "<your-subscription-id>"
export AKS_RESOURCE_GROUP="${USER}-dacs"
export DOCKER_REGISTRY="<your-acr>.azurecr.io"

make provision TARGET=aks AKS_TEMPLATE=nvme AKS_LOCATION=westus2
make deploy

# When finished, remove this personal cluster and its managed resource group.
make unprovision
```

Leave `AKS_RESOURCE_PREFIX` unset. This path creates a personal RG with the
existing subscription-scoped template, retrieves cluster credentials, and
preserves the local cleanup behavior. Your Azure identity needs resource
provisioning and the cluster network/storage/ACR role-assignment permissions.
You do not need the integration service connection, shared RG, pipeline tags,
or integration-specific role assignments.

### Integration pipeline

The [daily AKS integration pipeline](../test/README.md#azure-pipeline-dacs-integration)
deploys into the permanent `dacs-integration` resource group and resolves the
registry name from `DOCKER_REGISTRY`. Each suite/attempt has its own cluster,
VNet, storage account, and workload identity. The resource group must already
have `purpose=dacs-integration`; provisioning never creates or retags it.
Integration profiles supply node count, VM size, OS SKU, and zones; monitoring
is disabled and per-suite storage/workload identity are always provisioned.
The AKS Defender profile is enabled during cluster creation with the persistent
`dacs-integration-defender` Log Analytics workspace in the shared resource
group. Azure Policy owns the Azure Security Linux Agent extension so
provisioning does not race a second extension update.
Provisioning grants the AKS control-plane identity Network Contributor on the
cluster VNet. It grants the kubelet Container Registry Repository Reader for an
ABAC-enabled registry, or AcrPull for a classic RBAC registry. The provisioning
identity needs permission to create these role assignments. Lookup or assignment
failures stop provisioning.

Integration cleanup uses `make aks-clean AKS_RESOURCE_GROUP=dacs-integration
AKS_RESOURCE_PREFIX=<prefix>` with `DOCKER_REGISTRY` set. It checks ownership
tags, removes that kubelet's ACR pull assignments, waits for AKS deletion, and
deletes only the suite's auxiliary resources, detached NRMS policy-created
network security groups matching the run VNet, and deployment records. It
never deletes the shared RG. Omitting the prefix for `dacs-integration` is
rejected. The local `make provision` / `make unprovision` behavior for other
RGs is unchanged.
See [administrator setup](../test/README.md#azure-and-kubernetes-permissions)
for RG-scoped roles and conditional role-assignment delegation.

### Templates

Templates are defined as parameter files in `deploy/parameters/`. Available templates:

| Template | Description | VM Size |
|----------|-------------|---------|
| `nvme` | AKS cluster with Standard_L8s_v3 VMs with NVMe disks | Standard_L8s_v3 |
| `nvme-l8` | Performance testing cluster with L8s_v3 VMs | Standard_L8s_v3 |
| `nvme-l16` | Performance testing cluster with L16s_v3 VMs | Standard_L16s_v3 |
| `nvme-l32` | Performance testing cluster with L32s_v3 VMs | Standard_L32s_v3 |

The `nvme-perf-*` templates are optimized for performance testing with larger VM sizes and more NVMe storage capacity.

### Usage

```bash
# Create AKS cluster with default settings
make provision TARGET=aks

# Create AKS cluster in a specific region
make provision TARGET=aks AKS_LOCATION=westus2

# Preview what will be created (what-if)
make aks-cluster-requirements

# Get credentials for the cluster
make aks-get-credentials

# Install helm charts
make deploy

# Delete helm charts
make undeploy

# Delete the cluster (auto-detected from kubectl context)
make unprovision
```

## Helm Charts

### Parameters

| Parameter | Description | Default Value |
|-----------|-------------|---------------|
| `HELM_NAMESPACE` | Kubernetes namespace for cache-server | `cache-server` |
| `DOCKER_REGISTRY` | Docker registry for images | `docker.io` |
| `BUILD_NUMBER` | Image tag to use | `local` |
| `HELM_ARGS` | Additional arguments to pass to helm | - |

### Usage

```bash
# Install dacs-prereq (NVMe RAID setup)
make helm-prereq-install

# Install cache-server
make helm-install

# Install with custom image
DOCKER_REGISTRY=myregistry.azurecr.io BUILD_NUMBER=v1.2.3 make helm-install

# Uninstall cache-server
make helm-uninstall

# Uninstall dacs-prereq
make helm-prereq-uninstall
```

## Dependencies

The deployment targets automatically download required tools:

| Tool | Version | Description |
|------|---------|-------------|
| Bicep | v0.37.4 | Azure Bicep CLI for infrastructure deployment |
| Helm | v3.18.6 | Kubernetes package manager |

Tools are installed to `.local/bin/` in the project root.
