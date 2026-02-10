# Testing

This document explains the E2E test coverage for DACS cache-server.

## E2E

`make test-e2e` runs E2E tests against the cluster pointed to by your kubeconfig.

The test framework automatically detects the cluster type from the kubectl context:
- **Minikube**: Runs tests with label filter `e2e && !aks` (excludes AKS-only tests)
- **AKS/Other**: Runs tests with label filter `e2e` (all e2e tests)

### Setup

These tests are designed to run on a Kubernetes cluster with NVMe devices.
The test framework uses controller-runtime client for cluster interactions.

The test framework will:
1. Create the `cache-server` namespace
2. Deploy dacs-prereq (RAID setup) if `INSTALL_PREREQ=true`
3. Deploy cache-server (scheduler + StatefulSet) if `INSTALL=true`
4. Verify all pods are ready
5. Run test scenarios
6. Collect support bundle on completion

#### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `INSTALL` | `false` | Install cache-server helm chart before tests |
| `INSTALL_PREREQ` | `false` | Install dacs-prereq helm chart before tests |
| `UNINSTALL` | `false` | Uninstall cache-server helm chart after tests |
| `UNINSTALL_PREREQ` | `false` | Uninstall dacs-prereq helm chart after tests |
| `SKIP_SUPPORT_BUNDLE` | `false` | Skip collecting support bundle after tests |
| `DOCKER_REGISTRY` | - | Docker registry to pull images from |
| `BUILD_NUMBER` | - | Image tag to use for cache-server images |

#### Test Filtering

| Variable | Default | Description |
|----------|---------|-------------|
| `FOCUS` | - | Run only tests matching the pattern |
| `LABEL_FILTER` | - | Additional label filter (combined with auto-detected filter) |
| `TEST_TIMEOUT` | `60m` | Set test timeout |
| `NO_COLOR` | `false` | Disable colored output |

## Workflows

### Minikube Workflow (Local Development)

```sh
# Create minikube cluster
make provision TARGET=minikube

# Build and load images into minikube
make images load-images

# Install helm charts (uses local image tags)
make deploy

# Run e2e tests (auto-detects minikube, excludes AKS-only tests)
make test-e2e

# Clean up
make undeploy
make unprovision
```

### AKS Workflow (Cloud Deployment)

```sh
# Create AKS cluster
make provision TARGET=aks AKS_LOCATION=uksouth

# Attach ACR (in Azure Portal or CLI)
az aks update -g $AKS_RESOURCE_GROUP -n ${AKS_RESOURCE_GROUP}-cluster --attach-acr myacr

# Build and publish images
DOCKER_REGISTRY=myacr.azurecr.io BUILD_NUMBER=v1.0.0 make images publish-images

# Install helm charts (prereq for NVMe RAID setup, then cache-server)
DOCKER_REGISTRY=myacr.azurecr.io BUILD_NUMBER=v1.0.0 make deploy

# Run e2e tests (auto-detects AKS, runs all tests)
make test-e2e

# Clean up
make undeploy
make unprovision
```

## Support Bundle

`make get-support-bundle` collects diagnostic information from the cluster.

The support bundle includes:
- Cluster resources from `cache-server` namespace
- Helm release information (values, no secrets)
- Pod logs from `cache-server` namespace
- Kubernetes events (collected separately)

Output is saved to `support-bundles/` directory by default.

```sh
# Collect support bundle with default settings
make get-support-bundle

# Specify custom output directory
SUPPORT_BUNDLE_OUTPUT_DIR=/tmp/bundles make get-support-bundle

# Specify custom output file
SUPPORT_BUNDLE_OUTPUT=/tmp/my-bundle.tar.gz make get-support-bundle
```

## Performance Tests

`make test-perf` runs performance tests against the cluster pointed to by your kubeconfig.

Performance tests measure throughput and latency of the cache-server using configurable parameters.

### Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--data-size-gib` | `1` | Data size in GiB for perf tests |
| `--cache-server-count` | `1` | Number of cache servers to use |
| `--num-threads` | `1` | Number of threads for perf client |
| `--checksum-algorithm` | `xxhash32` | Checksum algorithm (`xxhash32` or `crc32`) |

### Performance Metrics

Performance tests capture detailed latency metrics including:
- **Throughput**: Overall data transfer rate (Gbps/Mbps)
- **Per-chunk latency percentiles**: p50, p90, p99 (milliseconds)
- **Success/failure counts**: Individual chunk transfer statistics
- **Duration**: Total test execution time

These metrics are automatically collected from the PerfClient logs and can be sent to EventHub for Kusto ingestion when `SEND_METRICS=true`.

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `SEND_METRICS` | `false` | Send metrics to EventHub for Kusto ingestion |
| `DOCKER_REGISTRY` | `dacs.azurecr.io` | Docker registry for perf client image |
| `BUILD_NUMBER` | - | Image tag for perf client |

### Examples

```sh
# Run perf tests with default settings (1 GiB, 1 thread)
make test-perf

# Run perf tests with 100 GiB data size and 32 threads
make test-perf EXTRA_TEST_ARGS="--data-size-gib=100 --num-threads=32"

# Run perf tests with 5 cache servers and crc32 checksum
make test-perf EXTRA_TEST_ARGS="--cache-server-count=5 --checksum-algorithm=crc32"

# Full example with all parameters
make test-perf EXTRA_TEST_ARGS="--data-size-gib=100 --cache-server-count=5 --num-threads=32 --checksum-algorithm=xxhash32"

# Send metrics to EventHub (requires Azure credentials)
SEND_METRICS=true make test-perf EXTRA_TEST_ARGS="--data-size-gib=100 --num-threads=32"
```

### AKS Performance Testing Workflow

```sh
# Create AKS cluster with NVMe nodes (L16s_v3 recommended for perf)
make provision TARGET=aks AKS_TEMPLATE=nvme-perf-l16 AKS_LOCATION=westus2

# Install prerequisites and cache-server
DOCKER_REGISTRY=dacs.azurecr.io BUILD_NUMBER=20251222.6 make deploy

# Run performance tests
DOCKER_REGISTRY=dacs.azurecr.io BUILD_NUMBER=20251222.6 make test-perf EXTRA_TEST_ARGS="--data-size-gib=100 --cache-server-count=5 --num-threads=32"

# Clean up
make undeploy
make unprovision
```

### AKS Templates for Performance Testing

| Template | VM Size | Description |
|----------|---------|-------------|
| `nvme-perf-l8` | Standard_L8s_v3 | 8 vCPUs, smaller workloads |
| `nvme-perf-l16` | Standard_L16s_v3 | 16 vCPUs, recommended for perf tests |
| `nvme-perf-l32` | Standard_L32s_v3 | 32 vCPUs, large scale testing |

## Test Framework

The E2E tests use [Ginkgo v2][ginkgo] as the testing framework with the following configuration:

- **Default Timeout**: 2 minutes for Eventually/Consistently assertions
- **Polling Interval**: 1 second for Eventually assertions
- **Controller-runtime Client**: Used for all Kubernetes API operations
- **Support Bundle**: Collected automatically at suite level in AfterSuite

See [e2e/e2e_suite_test.go](./e2e/e2e_suite_test.go) for suite configuration and [pkg/common/common.go](./pkg/common/common.go) for common test utilities.

### Test Structure

```
test/
├── config/                     # Test configuration files
│   └── support-bundle.yaml     # Support bundle collection spec
├── e2e/                        # E2E test cases
│   └── e2e_suite_test.go       # Suite setup and configuration
├── perf/                       # Performance test cases
│   └── perf_client_test.go     # Performance client tests
├── fixtures/                   # Test fixtures
├── pkg/
│   ├── common/                 # Common test utilities
│   │   └── common.go           # Setup, teardown, validation helpers
│   └── utils/                  # General utilities
└── test.mk                     # Test make targets
```

## CI/CD Integration

The E2E and performance tests are integrated into the Azure DevOps pull request pipeline:

1. Build binaries and Docker images
2. Create Minikube cluster
3. Deploy cache-server with `make provision TARGET=minikube && make deploy`
4. Run E2E tests with `make test-e2e`
5. Run performance tests with `make test-perf` (regression check)
6. Publish test results (JUnit XML)
7. Upload support bundle artifacts

Performance tests are run on Minikube in the PR pipeline to catch regressions before merge. For full-scale performance testing, use the AKS workflow described above.

See [.pipelines/dacs-pull-request-base.yml](../.pipelines/dacs-pull-request-base.yml) for PR pipeline configuration and [.pipelines/Create-Cacheserver-AKS-Test.yml](../.pipelines/Create-Cacheserver-AKS-Test.yml) for AKS performance test pipeline.

[ginkgo]: https://onsi.github.io/ginkgo/
