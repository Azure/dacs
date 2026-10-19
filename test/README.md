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
| `TEST_TIMEOUT` | `120m` | Set test timeout |
| `NO_COLOR` | `false` | Disable colored output |

## Workflows

### Azure Pipeline: dacs-integration

[dacs-integration](../.pipelines/dacs-integration.yml) runs on `main` every day
at 06:00 and 18:00 UTC. It runs performance, AKS upgrade, AKS scale, AKS node
crash, AKS restart, and Helm upgrade stages in parallel.

The node-crash test intentionally panics one node and waits for automatic
recovery. Run it only against a disposable test cluster.

To run it on demand:

1. Queue `dacs-integration` and select `dacsOfficial` or `dacsPullRequest`.
2. Under **Resources**, select the producer run to test.
3. Leave `cacheServerBuildId` at `latest` to use that run's
   `dacs-release` artifact, or enter an exact published version.

The default region is `australiaeast`. Test jobs have a 120-minute limit.
Cleanup runs separately and preserves the shared resource group.

If cleanup does not run, copy the prefix from **Publish Resource Prefix** and
run:

```bash
make aks-clean \
  AKS_RESOURCE_GROUP=dacs-integration \
  AKS_RESOURCE_PREFIX=<prefix>
```

For failures, select a producer run with a complete `dacs-release` artifact,
check both regional SKU restrictions and quota, and inspect the suite log and
support bundle. Do not bypass cleanup ownership checks or delete the shared
resource group.

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

### Standalone AKS Test Binary

Build the architecture-specific Ginkgo test executable with:

```sh
make aks-test-binary BUILD_ARCH=amd64
```

The output is written to:

```text
bin/Release/<architecture>/aks-tests/dacs-aks-tests
```

The executable is self-contained, but AKS tests still require a valid
kubeconfig and the external `az`, `helm`, and `kubectl` commands.

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

`make test-e2e TEST_FILTER=perf` runs performance tests against the cluster pointed to by your kubeconfig.

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
| `DOCKER_REGISTRY` | `dacstest.azurecr.io` | Docker registry for perf client image |
| `BUILD_NUMBER` | - | Image tag for perf client |

### Examples

```sh
# Run perf tests with default settings (1 GiB, 1 thread)
make test-e2e TEST_FILTER=perf

# Run perf tests with 100 GiB data size and 32 threads
make test-e2e TEST_FILTER=perf EXTRA_TEST_ARGS="--data-size-gib=100 --num-threads=32"

# Run perf tests with 5 cache servers and crc32 checksum
make test-e2e TEST_FILTER=perf EXTRA_TEST_ARGS="--cache-server-count=5 --checksum-algorithm=crc32"

# Full example with all parameters
make test-e2e TEST_FILTER=perf EXTRA_TEST_ARGS="--data-size-gib=100 --cache-server-count=5 --num-threads=32 --checksum-algorithm=xxhash32"

# Send metrics to EventHub (requires Azure credentials)
SEND_METRICS=true make test-e2e TEST_FILTER=perf EXTRA_TEST_ARGS="--data-size-gib=100 --num-threads=32"
```

### AKS Performance Testing Workflow

```sh
# Create AKS cluster with NVMe nodes (L16s_v3 recommended for perf)
make provision TARGET=aks AKS_TEMPLATE=nvme-l16 AKS_LOCATION=westus2

# Install prerequisites and cache-server
DOCKER_REGISTRY=dacstest.azurecr.io BUILD_NUMBER=20251222.6 make deploy

# Run performance tests
DOCKER_REGISTRY=dacstest.azurecr.io BUILD_NUMBER=20251222.6 make test-e2e TEST_FILTER=perf EXTRA_TEST_ARGS="--data-size-gib=100 --cache-server-count=5 --num-threads=32"

# Clean up
make undeploy
make unprovision
```

### AKS Templates for Performance Testing

| Template | VM Size | Description |
|----------|---------|-------------|
| `nvme-l8` | Standard_L8s_v3 | 8 vCPUs, smaller workloads |
| `nvme-l16` | Standard_L16s_v3 | 16 vCPUs, recommended for perf tests |
| `nvme-l32` | Standard_L32s_v3 | 32 vCPUs, large scale testing |

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

The public GitHub workflow runs credential-free unit, Minikube E2E, and
Minikube performance tests. The official Azure DevOps pipeline runs the
Azure-backed storage suites after successful AMD64 and ARM64 builds:

| Suite | Pipeline stage | Architectures | Local command |
|-------|----------------|---------------|---------------|
| Native storage intercept | `AzureTests_<arch>` | AMD64, ARM64 | `test_serverless.sh`, `test_cache_basic.sh`, `test_cache_advanced.sh`, and `test_cache_specialized.sh` |
| Python Azure Blob | `AzureTests_<arch>` | AMD64, ARM64 | `DACS_TEST_MODE=wheel make pytest BUILD_TYPE=Release` |
| Rust Azure Blob | `AzureTests_<arch>` serverless job | AMD64, ARM64 | `DACS_TEST_BLOB_PATH=<az-path> make test-rust-client-integration` |
| InfiniBand | `InfiniBandTests` | AMD64 | `test_ib.sh Release` |

Azure Pipelines runs the advanced cache suite as `cache` and `encryption`
shards, and the specialized suite as `snapshot`, `eviction`, and `remote`
shards. Set `DACS_TEST_GROUP` to one of those values to run a shard directly;
the default `all` value preserves the complete local suite. Whenever
`dacs-pullrequest` is queued, every shard runs for both AMD64 and ARM64.

The official pipeline runs automatically on `main` and on its weekly Sunday
schedule.
It does not have an automatic PR trigger. Authorized internal users can
manually queue a PR branch; credentialed test stages only run for `main` or a
manual build. Manual queue permission must remain restricted to trusted
internal users because the selected branch owns the test scripts that receive
the short-lived credential. The release publication stage remains restricted
to `main`.

`enableAzureTests` defaults to `true`. `azureStorageBlobPath` defaults to
`az://dacstest.blob.core.windows.net/dataset/client-integration`. The native,
Python, and Rust tests use deterministic synthetic fixtures in the `dataset`
and `dataset2` containers of that account. Python tests create an isolated
per-run prefix and delete it during cleanup.

Generate the native fixtures and manifests with:

```bash
python3 .pipelines/scripts/tests/generate-azure-test-fixtures.py \
  --output-dir /tmp/dacs-azure-fixtures \
  --manifest-dir src/subsystems/storage/storage_intercept/integration_tests \
  --unit-manifest \
    src/subsystems/storage/storage_intercept/tests/testcases/file_paths_sizes.txt \
  --directory-manifest \
    src/subsystems/storage/storage_intercept/tests/testcases/directory_paths.txt \
  --account-name dacstest \
  --upload
```

The `--upload` option uses the current Azure CLI login to create the `dataset`
and `dataset2` containers, upload all generated fixtures, and verify every
uploaded blob and size. This includes native unit and integration fixtures,
archives, multi-container data, and the Rust client fixture. Omit `--upload` to
generate files and manifests without changing Azure. Python tests create and
clean up their own per-run blobs. The signed-in identity requires
`Storage Blob Data Contributor` on the test account. Use a dedicated test-only
identity with no production scope.

`enableInfiniBandTests` defaults to `false`. Before enabling it, set
`infiniBandPoolName` and `infiniBandPoolImage` to a 1ES pool with InfiniBand
hardware, drivers, device access, and Azure connectivity.

The official pipeline does not provision AKS or run AKS scenario, upgrade,
scale, restart, Helm upgrade, or full-scale performance tests.

Python results appear as architecture-qualified test runs. Native storage jobs
print their existing diagnostics in the job log, matching the established PR
pipeline structure.

See
[`.pipelines/dacs-official.yml`](../.pipelines/dacs-official.yml)
for the official pipeline configuration.

[ginkgo]: https://onsi.github.io/ginkgo/
