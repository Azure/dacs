# AGENT.md - AI Agent Guidelines for DACS (dacs)

## Project Overview

DACS (internally "dacs") is a high-performance distributed caching system for Kubernetes. It provides storage caching layer components for DACS, including CacheServer, CacheClient, and StorageInterception libraries.

### Key Components

| Component | Language | Location | Description |
|-----------|----------|----------|-------------|
| **CacheServer** | C++ | `src/subsystems/storage/cache_server/` | Distributed cache server storing data on local NVMe/SSD |
| **CacheClient** | C++ | `src/subsystems/storage/cache_client/` | Client library for cache communication |
| **CacheServerScheduler** | C++ | `src/subsystems/storage/cache_server_scheduler/` | Custom Kubernetes scheduler for cache-aware pod placement |
| **StorageIntercept** | C++ | `src/subsystems/storage/storage_intercept/` | Storage interception layer |
| **Cache Controller** | Go | `src/subsystems/cache-controller/` | Kubernetes controller for cache CRDs |
| **E2E/Perf Tests** | Go | `test/` | End-to-end and performance tests |

---

## Technology Stack

### Languages & Standards
- **C++23** - Primary language for native components (cache server, client, scheduler)
- **Go 1.24+** - Kubernetes controller and test framework
- **Python** - Integration tests and utilities
- **Bash** - Build and deployment scripts

### Build System
- **CMake 3.19+** - Native C++ build system
- **Make** - Top-level build orchestration
- **Ninja** - CMake backend for faster builds

### Key Dependencies
- **Boost** - C++ utilities
- **spdlog** - Logging
- **nlohmann/json** - JSON serialization (supports BSON, CBOR)
- **Google Test** - C++ unit testing
- **Prometheus C++** - Metrics
- **Azure Storage Lite** - Blob service communication
- **protobuf** - Protocol buffers

### Kubernetes Stack
- **Helm 3** - Deployment charts
- **controller-runtime** - Go controller framework
- **Ginkgo/Gomega** - Go BDD testing framework
- **Bicep** - Azure infrastructure as code

---

## Repository Structure

```
├── src/subsystems/
│   ├── storage/             # Native C++ storage components
│   │   ├── cache_server/
│   │   ├── cache_client/
│   │   ├── cache_server_scheduler/
│   │   ├── storage_intercept/
│   │   ├── common/
│   │   ├── dacs_client/  # python client
│   │   ├── tools/           # debugging tools
│   │   └── utils/
│   ├── cache-controller/    # Go Kubernetes controller
│   └── common/native_lib/   # Shared C++ libraries
├── helm/
│   ├── cache-server/        # Main deployment chart
│   └── dacs-prereq/ # RAID setup daemonset
├── deploy/
│   ├── template/            # Bicep templates for AKS
│   ├── parameters/          # AKS parameter files
│   └── scripts/             # Cluster provisioning scripts
├── test/
│   ├── e2e/                 # End-to-end tests
│   ├── perf/                # Performance tests
│   └── pkg/                 # Test utilities
└── Docker/                  # Dockerfiles
```

---

## Build Commands

### Full Build (Containerized - Default)
```bash
make build                      # Debug build
make build BUILD_TYPE=Release   # Release build
make clean                      # Clean build artifacts
```

### Native Build (Without Container)
```bash
make build USE_CONTAINER=0
```

### Docker Images
```bash
make images                     # Build all Docker images
make publish-images             # Push images to registry
```

### Helm Charts
```bash
make helm-build                 # Build helm charts
make helm-push                  # Push to registry
```

---

## Testing Commands

### Unit Tests
```bash
make test                       # Run all unit tests (native + Go)
make test-native                # Run C++ unit tests only
make test-go                    # Run Go unit tests only
make pytest                     # Run Python unit tests
make test TEST="pattern"        # Run specific tests matching pattern
```

### E2E Tests
```bash
make test-e2e                   # Run e2e tests against current kubeconfig
make test-perf                  # Run performance tests
```

### Test Options
| Variable | Description |
|----------|-------------|
| `FOCUS=<pattern>` | Run only tests matching pattern |
| `LABEL_FILTER=<filter>` | Additional label filter |
| `TEST_TIMEOUT=90m` | Override default 60m timeout |
| `EXTRA_TEST_ARGS='--flag=val'` | Pass extra args to tests |

---

## Deployment Commands

### Cluster Provisioning
```bash
make provision TARGET=minikube  # Create local minikube cluster
make provision TARGET=aks       # Create AKS cluster
make unprovision                # Delete cluster (auto-detects type)
```

### Helm Deployment
```bash
make deploy                     # Install cache-server helm charts
make undeploy                   # Uninstall helm charts
make load-images                # Load images into minikube
```

### AKS Options
| Variable | Default | Description |
|----------|---------|-------------|
| `AKS_LOCATION` | `eastus` | Azure region |
| `AKS_TEMPLATE` | `nvme` | Parameter template (nvme, nvme-l8, etc.) |
| `AKS_RESOURCE_GROUP` | auto | Resource group name |

---

## Development Environment

### Option 1: Devcontainer (Recommended)
1. Open repository in VS Code
2. Run "Dev Containers: Reopen in Container"
3. Wait for container build (~10-15 minutes first run)

### Option 2: WSL2 Local Setup
```bash
./dev-setup.sh                  # Install all dependencies
```

### Build Environment Script
```bash
./bldenv.sh COMMAND             # Run command in build container
```

---

## Code Style & Conventions

### C++ Code
- Standard: **C++23**
- Compiler: **Clang** with `-Werror` (warnings as errors)
- Naming: Follow existing patterns in codebase
- Headers in corresponding `.h` files

### Go Code
- Standard: **Go 1.24+**
- Use `controller-runtime` patterns for Kubernetes controllers
- Tests use **Ginkgo/Gomega** BDD framework

### Commit Guidelines
- Use conventional commit messages
- Reference Azure DevOps work items when applicable

---

## Key Files Reference

| File | Purpose |
|------|---------|
| `Makefile` | Top-level build orchestration |
| `CMakeLists.txt` | CMake build configuration |
| `deploy/deploy.mk` | Cluster provisioning targets |
| `test/test.mk` | Test execution targets |
| `bldenv.sh` | Build environment container script |
| `toolchain.cmake` | CMake toolchain configuration |
| `helm/cache-server/values.yaml` | Helm chart defaults |

---

## Common Workflows

### Adding a New C++ Component
1. Create directory under `src/subsystems/storage/`
2. Add `CMakeLists.txt` with target definitions
3. Add `add_subdirectory()` in root `CMakeLists.txt`
4. Add tests in corresponding `.tests/` directory

### Modifying Kubernetes Controller
1. Edit code in `src/subsystems/cache-controller/`
2. Run `make test-go` to verify
3. Run `make -C src/subsystems/cache-controller docker-build` for image

### Running Local E2E Tests
```bash
make provision TARGET=minikube
make images
make load-images
make deploy
make test-e2e
make undeploy
make unprovision
```

---

## Troubleshooting

### Build Issues
- Ensure Docker is running for containerized builds
- Use `make clean` before rebuilding after major changes
- Check `USE_CONTAINER=0` for native build issues

### Test Issues
- Verify kubeconfig points to correct cluster
- Check `kubectl get pods -n cache-server` for pod status
- Use `make get-support-bundle` to collect diagnostics

### Deployment Issues
- Ensure NVMe devices available on nodes (for AKS NVMe templates)
- Check RAID setup via `dacs-prereq` chart
- Review scheduler logs for placement issues

---

## Environment Variables

| Variable | Description |
|----------|-------------|
| `BUILD_TYPE` | Build type: Debug (default) or Release |
| `BUILD_ARCH` | Target architecture: amd64, arm64 |
| `USE_CONTAINER` | Use containerized build: 1 (default) or 0 |
| `DOCKER_REGISTRY` | Docker registry for images |
| `BUILD_NUMBER` | Image tag for builds |

---

## Additional Resources

- [README.md](README.md) - Full developer setup guide
- [deploy/README.md](deploy/README.md) - Deployment documentation
- [test/README.md](test/README.md) - Testing documentation
- [helm/cache-server/README.md](helm/cache-server/README.md) - Helm chart documentation
