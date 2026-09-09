# Distributed Accelerator for Cloud Storage (DACS)

Distributed Accelerator for Cloud Storage (DACS) is a distributed caching
system for data-intensive workloads on Kubernetes. It places a high-throughput
cache between applications and remote object storage. Cache servers use
node-local storage to reduce repeated remote reads and improve data access
throughput.

DACS is intended for Kubernetes users who operate data-heavy workloads and for
contributors working on the cache data plane, clients, or Kubernetes
management components.

> [!IMPORTANT]
> DACS is under active development and should currently be treated as
> pre-release software. APIs, configuration, and deployment behavior may
> change.

> [!WARNING]
> **Security model:** DACS currently assumes a single trust domain equal to the
> entire Kubernetes cluster. It does not yet provide tenant isolation and is
> **not suitable for multi-tenant clusters**. Any workload that can reach a DACS
> endpoint is treated as fully trusted with all cached data and operations.
> Read the **[security model](docs/security-model.md)** before deploying.

## Choose your path

### Get running in 10 minutes

Follow the **[AKS quickstart](docs/quickstart.md)** to install one ephemeral
CacheServer on an existing AKS cluster and complete an end-to-end upload and
download smoke test.

The published OCI charts and images at `dacstest.azurecr.io` are publicly readable
and can be pulled anonymously.

### Deploy DACS for production

Use the **[deployment user guide](user-guide.md)** to prepare local NVMe/RAID
storage, install matching chart and image versions, configure a cache, verify
the deployment, scale it, troubleshoot it, and uninstall it.

### Build and test changes

Use **[Build from source](docs/build-from-source.md)** for a clean-clone build,
targeted validation, build outputs, and common build failures. See
**[Testing](test/README.md)** for integration, end-to-end, scenario, and
performance test workflows.

## How DACS works

Applications access data through CacheClient, the DACS Python client, or
StorageIntercept. Cache servers store cache entries on local storage. On a
cache miss, the storage interception path can read supported remote storage and
populate the cache.

The Kubernetes cache controller creates and reconciles cache workloads from a
`Cache` custom resource. The CacheServer scheduler places host-storage cache
pods on suitable nodes. These management components are not in the data
transfer path.

```text
Application
    |
    +-- CacheClient / DACS client / StorageIntercept
          |-- cache lookup --------> CacheServer StatefulSet
          |                              |
          |                              +-- node-local storage
          |-- on cache miss: read --> Azure Blob / remote object storage
          +-- may populate cache ---> CacheServer StatefulSet

Kubernetes API
    |
    +-- Cache controller
    |
    +-- CacheServer scheduler
```

See [Architecture](docs/architecture.md) for the detailed request flow.

## Documentation map

| Goal or component | Documentation |
| --- | --- |
| Evaluate DACS on AKS | [Get running in 10 minutes](docs/quickstart.md) |
| Understand the system architecture | [Architecture](docs/architecture.md) |
| Understand the security model and trust boundary | [Security model](docs/security-model.md) |
| Install and operate DACS | [Deployment user guide](user-guide.md) |
| Build from a clean clone | [Build from source](docs/build-from-source.md) |
| Run tests | [Testing](test/README.md) |
| Understand artifact versions and releases | [Versioning and releases](VERSIONING.md) |
| Use deployment tooling | [Deployment workflows](deploy/README.md) |
| Configure the CacheServer chart | [CacheServer Helm chart](helm/cache-server/README.md) |
| Configure the controller and `Cache` resource | [Cache controller Helm chart](src/subsystems/cache-controller/charts/dacs/README.md) |
| Understand controller scaling | [Cache controller scaling](src/subsystems/cache-controller/docs/controller/scaling.md) |
| Use the Python client | [DACS Python client](src/subsystems/storage/dacs_client/README.md) |
| Configure storage interception | [StorageIntercept](src/subsystems/storage/storage_intercept/README.md) |

## Platforms and source distribution

The build and runtime code targets Linux on `amd64` and `arm64`. Documented
developer environments include Linux containers and Ubuntu under WSL2.
Kubernetes workflows cover Minikube and Azure Kubernetes Service (AKS). Native
Windows and macOS builds are not currently documented or supported.

Published OCI charts and container images at `dacstest.azurecr.io` are anonymously
pullable. Language packages are not currently published to public package
indexes; build them from this source distribution.

## Contributing, security, and support

- Read [CONTRIBUTING.md](CONTRIBUTING.md) before proposing a change.
- Follow the [Microsoft Open Source Code of Conduct](CODE_OF_CONDUCT.md).
- Report vulnerabilities using the private process in
  [SECURITY.md](SECURITY.md), not through a public issue.
- Understand the assumed trust boundary before deploying: see the
  [security model](docs/security-model.md).
- Use the channels in [SUPPORT.md](SUPPORT.md) for questions, bugs, and feature
  requests.

## Trademarks

This project may contain trademarks or logos for projects, products, or
services. Authorized use of Microsoft trademarks or logos is subject to and
must follow [Microsoft's Trademark & Brand
Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general).
Use of Microsoft trademarks or logos in modified versions of this project must
not cause confusion or imply Microsoft sponsorship. Any use of third-party
trademarks or logos is subject to those third parties' policies.
