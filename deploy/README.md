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

### Templates

Templates are defined as parameter files in `deploy/parameters/`. Available templates:

| Template | Description |
|----------|-------------|
| `nvme` | AKS cluster with Standard_L8s_v3 VMs with NVMe disks |

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
