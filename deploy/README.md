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

For the new controller, install the charts and then apply the desired Cache
manifest directly:

```bash
make deploy CONTROLLER=new
kubectl apply -n cache-server \
  -f src/subsystems/cache-controller/charts/dacs/examples/cache/static.yaml
kubectl wait -n cache-server \
  -f src/subsystems/cache-controller/charts/dacs/examples/cache/static.yaml \
  --for=condition=Ready --timeout=10m
```

Use the dynamic example instead when needed. Cache manifests are not managed
by Make targets. The controller chart blocks uninstall while Cache resources
remain. Delete them first, or explicitly enable `cleanup.forceDeleteCaches`
on the release before uninstalling; see the
[chart uninstall instructions](../src/subsystems/cache-controller/charts/dacs/README.md#uninstall).

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
| `AKS_RESOURCE_PREFIX` | Integration-only `aks-<suite>-<build>-<stage-attempt>-<job-attempt>` prefix; requires the existing `dacs-integration` RG and `AKS_IS_TEST=true` | Empty (local subscription deployment) |

### Templates

Templates are defined as parameter files in `deploy/parameters/`. Available templates:

| Template | Description | VM Size |
|----------|-------------|---------|
| `nvme` | AKS cluster with Standard_L8s_v3 VMs with NVMe disks | Standard_L8s_v3 |
| `nvme-l8` | Performance testing cluster with L8s_v3 VMs | Standard_L8s_v3 |
| `nvme-l16` | Performance testing cluster with L16s_v3 VMs | Standard_L16s_v3 |
| `nvme-l32` | Performance testing cluster with L32s_v3 VMs | Standard_L32s_v3 |
| `nvme-mixed` | Seven AMD64 nodes across six Linux OS/FIPS pools for correctness tests | Standard_L8s_v3 |

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

### Mixed Linux Integration Profile

`AKS_TEMPLATE=nvme-mixed` selects seven AMD64 nodes across six OS/FIPS
profiles. Correctness stages use this template; performance/scenario stages
use their selected single-pool template. The full pool list is documented in the
[integration test guide](../test/README.md#mixed-linux-node-images).

The parameter file supplies `nodePools` overrides and `kubernetesVersion`.
Each pool inherits the shared NVMe settings, including VM size, zones, subnet,
and CacheServer node label. Parameter files without `nodePools` use one pool.

For an owned disposable integration run:

```bash
make provision TARGET=aks AKS_IS_TEST=true \
  AKS_RESOURCE_GROUP=dacs-integration \
  AKS_RESOURCE_PREFIX='aks-scale-<build>-<stage-attempt>-<job-attempt>' \
  AKS_TEMPLATE=nvme-mixed AKS_LOCATION=australiaeast
```

Replace the prefix placeholders with the integration run's identifiers.
`make aks-cluster-requirements` accepts the same inputs for what-if.
The mixed template pins Kubernetes `1.35` and disables OS image auto-upgrades;
the upgrade test stays within the common 1.34-1.36 support window.

The ACL pool enables Secure Boot and vTPM. Its storage initialization requires
live validation. The shared AKS template uses managed-cluster API `2026-08-01`;
older Bicep type bundles report `BCP081`, so compilation alone does not validate
its resource properties.

## Helm Charts

### Parameters

| Parameter | Description | Default Value |
|-----------|-------------|---------------|
| `HELM_NAMESPACE` | Kubernetes namespace for cache-server | `cache-server` |
| `DOCKER_REGISTRY` | Docker registry for images | `docker.io` |
| `BUILD_NUMBER` | Image tag to use | `local` |
| `HELM_ARGS` | Additional arguments to pass to helm | - |
| `HELM_TIMEOUT` | Helm install/upgrade timeout; keep longer than the prerequisite rollout-check timeout | `15m` |
| `CACHE_SERVER_PREREQ_ENABLED` | Enable bundled NVMe prerequisites | `false`; `make deploy` enables it on AKS |
| `CACHE_SETUP_IMG` | NVMe setup image repository | `$(DOCKER_REGISTRY)/dacs/cache-setup` |

### Usage

```bash
# Install cache-server without prerequisites
make helm-install

# Install either parent chart with NVMe prerequisites
make helm-install CACHE_SERVER_PREREQ_ENABLED=true
make helm-dacs-install CACHE_SERVER_PREREQ_ENABLED=true

# Install with custom image
DOCKER_REGISTRY=myregistry.azurecr.io BUILD_NUMBER=v1.2.3 make helm-install

# Uninstall cache-server and its bundled prerequisites (host data is retained)
make helm-uninstall
```

Both charts default prerequisites to off. AKS `make deploy` and the AKS test
pipeline opt in explicitly; Minikube deployments do not. See the
[prerequisite configuration](../helm/dacs-prereq/README.md) for rollout
limits and validation-hook behavior.

`make helm-dacs-install` and `make deploy CONTROLLER=new` install the
controller and optional prerequisites without creating a Cache. Wait for
prerequisite setup to succeed, then apply a standalone Cache manifest, as shown in the
[user guide](../user-guide.md#step-2-deploy-a-cache-instance).
Minikube needs no NVMe setup and uses the standalone Minikube Cache fixture.

## Dependencies

The deployment targets automatically download required tools:

| Tool | Version | Description |
|------|---------|-------------|
| Bicep | v0.37.4 | Azure Bicep CLI for infrastructure deployment |
| Helm | v3.18.6 | Kubernetes package manager |

Tools are installed to `.local/bin/` in the project root.
