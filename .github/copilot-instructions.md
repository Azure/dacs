# GitHub Copilot Instructions for DACS

## Project Context

DACS (dacs) is a high-performance distributed caching system for Kubernetes. This repository contains storage cache layer components including CacheServer, CacheClient, and StorageInterception libraries.

## Languages & Standards

### C++ (Primary - Native Components)
- **Standard**: C++23
- **Compiler**: Clang with `-Werror` (warnings as errors)
- **Build System**: CMake 3.19+ with Ninja backend
- **Location**: `src/subsystems/storage/`

When writing C++ code:
- Follow existing naming conventions in the codebase
- Use `spdlog` for logging
- Use `nlohmann/json` for JSON serialization
- Use Google Test for unit tests
- Prefer smart pointers over raw pointers
- Use `std::expected` or error codes for error handling (no exceptions in hot paths)

### Go (Kubernetes Controller & Tests)
- **Version**: Go 1.24+
- **Framework**: controller-runtime for Kubernetes controllers
- **Testing**: Ginkgo/Gomega BDD framework
- **Location**: `src/subsystems/cache-controller/`, `test/`

When writing Go code:
- Follow standard Go conventions and `go fmt`
- Use controller-runtime patterns for Kubernetes resources
- Use Ginkgo `Describe`/`It` blocks for tests
- Handle errors explicitly, don't ignore them

### Python (Integration Tests)
- Used for pytest-based integration tests
- Location: Various test directories

### Bash (Build & Deployment Scripts)
- Use `set -euo pipefail` for error handling
- Quote variables properly
- Location: `.pipelines/`, `deploy/scripts/`

## Key Dependencies

### C++ Libraries
- **Boost**: General utilities
- **spdlog**: Structured logging
- **nlohmann/json**: JSON/BSON/CBOR serialization
- **Google Test (gtest)**: Unit testing
- **Prometheus C++**: Metrics exposition
- **Azure Storage Lite**: Azure Blob communication
- **protobuf**: Protocol buffer serialization
- **xxHash**: Fast hashing

### Go Libraries
- **controller-runtime**: Kubernetes controller SDK
- **client-go**: Kubernetes client
- **Ginkgo/Gomega**: BDD testing
- **Azure SDK for Go**: Azure services

### Infrastructure
- **Helm 3**: Kubernetes deployments
- **Bicep**: Azure infrastructure as code
- **Docker**: Container images

## Project Structure

```
src/subsystems/storage/     # C++ storage components
  cache_server/             # Main cache server
  cache_client/             # Client library
  cache_server_scheduler/   # Custom K8s scheduler
  storage_intercept/        # Storage interception
  common/                   # Shared code
  utils/                    # Utilities

src/subsystems/cache-controller/  # Go K8s controller

helm/cache-server/          # Helm deployment chart
deploy/                     # Infrastructure & scripts
test/                       # E2E and performance tests
Docker/                     # Dockerfiles
```

## Build Commands

```bash
make build                      # Debug build (containerized)
make build BUILD_TYPE=Release   # Release build
make test                       # All unit tests
make test-native                # C++ tests only
make test-go                    # Go tests only
make images                     # Build Docker images
make deploy                     # Deploy to Kubernetes
make test-e2e                   # Run E2E tests
```

## Code Patterns

### C++ Cache Operations
```cpp
// Use the established patterns for cache operations
#include "CacheClient.h"

// Error handling with status codes
Status status = cache_client->Get(key, &value);
if (!status.ok()) {
    SPDLOG_ERROR("Cache get failed: {}", status.message());
    return status;
}
```

### Go Kubernetes Controller
```go
// Use controller-runtime reconciliation pattern
func (r *CacheReconciler) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
    log := log.FromContext(ctx)
    
    // Fetch the resource
    var cache cachev1.Cache
    if err := r.Get(ctx, req.NamespacedName, &cache); err != nil {
        return ctrl.Result{}, client.IgnoreNotFound(err)
    }
    
    // Reconciliation logic here
    return ctrl.Result{}, nil
}
```

### Ginkgo Tests
```go
var _ = Describe("CacheServer", func() {
    Context("when deployed", func() {
        It("should become ready", func() {
            Eventually(func() bool {
                return isPodReady(podName)
            }).Should(BeTrue())
        })
    })
})
```

## Testing Guidelines

- Unit tests should be fast and not require external dependencies
- E2E tests run against a real Kubernetes cluster
- Use `FOCUS=<pattern>` to run specific tests
- Performance tests use the `perf` label filter

## Common Tasks

### Adding a New C++ Component
1. Create directory under `src/subsystems/storage/`
2. Add `CMakeLists.txt` with target definitions
3. Register in root `CMakeLists.txt` with `add_subdirectory()`
4. Add unit tests in `.tests/` subdirectory

### Adding a Kubernetes CRD
1. Define types in `src/subsystems/cache-controller/api/`
2. Generate manifests: `make manifests`
3. Implement controller in `internal/controller/`
4. Add RBAC annotations as needed

### Modifying Helm Charts
1. Edit templates in `helm/cache-server/templates/`
2. Update `values.yaml` for new parameters
3. Test with `helm template` before deploying

## Environment Variables

| Variable | Description |
|----------|-------------|
| `BUILD_TYPE` | Debug or Release |
| `BUILD_ARCH` | amd64 or arm64 |
| `USE_CONTAINER` | 1 for containerized build, 0 for native |
| `DOCKER_REGISTRY` | Target container registry |
| `BUILD_NUMBER` | Image tag for builds |
