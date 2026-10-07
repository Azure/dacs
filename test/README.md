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

### Azure Pipeline: dacs-integration

[dacs-integration](../.pipelines/dacs-integration.yml) runs at 06:00 and 18:00 UTC daily
on `main`, even when there are no new commits. It selects the latest successful
`dacs-official` run once and consumes that run's `dacs-release` artifact.
Each suite reads the same selected release metadata to resolve the exact image
and chart versions. Test code and the prerequisite chart come from the
integration run's checkout. Application charts and images are pulled from ACR.

For an on-demand run, queue `dacs-integration` in Azure DevOps:

1. Set `buildPipeline` to `dacsOfficial` (`dacs-official`) or `dacsPullRequest`
   (`dacs-pullrequest`).
2. Under **Resources**, select the desired run of that pipeline. For a PR,
   first run `/azp run dacs-pullrequest` and wait for successful image and chart
   publication, then select that completed run.
3. Leave `cacheServerBuildId` at `latest` to consume the selected run's
   `dacs-release` metadata.

All suites use the selected run's image and chart versions, regardless of the
integration checkout branch or trigger. Integration never derives versions from
its own checkout, rebuilds images, or falls back to another producer when the
selected artifacts are missing. Missing artifacts or publications fail the run.
The default is `dacsOfficial`, including scheduled runs and
`/azp run dacs-integration`. The comment command does not select a PR producer;
use the Azure DevOps run dialog to choose `dacsPullRequest` and its run.

The `cacheServerBuildId` parameter can select an exact published DACS container
version explicitly; its default `latest` is a resolution mode, not an image tag.
An explicit version bypasses the selected pipeline's release metadata.

The four stages run in parallel in the permanent `dacs-integration` resource
group, each with a fresh six-node AKS cluster. Resource prefixes include the
suite, build ID, stage attempt, and job attempt, so concurrent runs and retries
do not share clusters, VNets, storage accounts, or workload identities.
AKS creates and manages a separate node resource group for each cluster.

| Stage | Package and labels | Configuration |
|-------|--------------------|---------------|
| Performance and scenarios | `test/perf`, `perf \|\| scenario` | L16s_v3, five static cache servers, zero-copy enabled, 100 GiB, 32 threads |
| AKS upgrade | `test/aks`, `aks && aks-upgrade` | L8s_v3, dynamic scaling at 100% of nodes, default AKS version |
| AKS scale | `test/aks`, `aks && aks-scale` | L8s_v3, dynamic scaling at 100% of nodes |
| Helm upgrade and rollback | `test/aks`, `aks && helm-upgrade` | L8s_v3, dynamic scaling at 100% of nodes, configured baseline chart upgraded to the selected target |

Performance tests use `xxhash32`, four adapter processes, and one 1-GiB blob.
Model-loading scenarios use the `7B` scale with `blobfile`, `boostedblob`,
`python`, `interceptor`, `huggingface`, `runaistreamer`, and `blobfuse` access
methods in `no_cache`, `cold_cache`, and `warm_cache` modes.

The Azure DevOps run form exposes `perfAksTemplate` (`nvme-l8`, `nvme-l16`,
`nvme-l32`), `scalingMode` (`dynamic`, `static`), `cacheServerCount`,
`checksumAlgorithm`, `dataSizeGiB`, `numThreads`, `numProcesses`, `numBlobs`,
`blobSizeGB`, `scenarioScale`, `scenarioMethods`, `scenarioCacheModes`, and
`helmChart` (`dacs`, `cache-server`). These defaults are used for scheduled
and PR-comment runs. `helmChart` selects the performance deployment only;
resilience tests always use the DACS controller. `scalingMode` selects the
Helm values for all three resilience stages. Keep it `dynamic` for the node-pool
scale test, which requires replicas to follow the node count; `static` is
available for investigating static-topology behavior but will not satisfy
that test's scale-down assertions.

Each stage provisions its cluster, installs prerequisites, deploys the chart,
runs tests, publishes results and diagnostics, and deletes the cluster.
`make helm-dacs-install` installs the controller and a Helm-managed Cache CR.
The Cache must become Ready before tests start. The Helm upgrade stage installs
the baseline chart using the selected static or dynamic values, then upgrades
to the resolved target and rolls back.

Functional E2E tests are not included in this pipeline. Run them separately
with `make test-e2e TEST_PACKAGES=./test/e2e TEST_FILTER=e2e`.

All stages are part of one pipeline; `/azp run` does not select a subset or
pass runtime parameters. Suites have no stage dependencies, so a failure in one
does not block the others. Each suite must resolve its versions successfully
before provisioning. Cancellation prevents queued stages from starting.
Running all four at once requires four available agent/parallel-job slots and
subscription quota for 24 initial nodes plus scale and upgrade surge capacity.
Tests run without Ginkgo parallel workers. Existing test skips, including
an AKS upgrade skipped when no upgrade or surge capacity is available, remain
visible in JUnit.

Each suite publishes its own JUnit results, generated diagnostic links, and
available support bundle. The artifact directory is created even when deployment
fails before tests start; missing support bundles do not add a copy failure.
Metrics include the integration run and branch identifiers. EventHub publishing
is temporarily disabled; metrics are still printed in the test logs.
Cleanup runs on failure and cancellation using `make aks-clean` with the shared
RG and the exact suite/attempt prefix. It requires `purpose=dacs-integration`
on the RG, and checks `createdBy=cache-server-tests`,
`managedBy=cache-server-infra`, and the exact `integrationRun` tag before
deleting resources. A mismatched owner or unexpected resource type fails
cleanup. An absent group is a no-op; Azure lookup errors fail cleanup.
The script removes the kubelet's ACR pull assignments, waits for cluster
deletion, and deletes the owned VNet, storage account, workload identity, and
deployment records. It also deletes detached NRMS policy-created network
security groups whose names end with the exact run VNet name. It never deletes
the shared RG or another suite's resources. Agent loss or interrupted cleanup
can leave resources behind. Rerun cleanup with the original prefix rather than
deleting the shared RG.

#### Pipeline configuration

Register the pipeline against `.pipelines/dacs-integration.yml` and configure:

| Setting | Purpose |
|---------|---------|
| `dockerRegistry` parameter | Registry host for published images and charts; default `dacstest.azurecr.io` |
| `helmUpgradeBaselineVersion` parameter | Baseline chart version; defaults to `0.1.0-alpha.0.113+5f43ac1cc927f9` from successful official build `20261006.1` |
| `location` parameter | AKS region; default `westus2`, with capacity for all four concurrent clusters and scale/upgrade surge |

ACR and baseline parameters can be overridden when queued in Azure DevOps.
Scheduled runs use the configured defaults. The pinned baseline comes from
[official build 20261006.1](https://dev.azure.com/msazure/One/_build/results?buildId=184384451).
Keep its chart and images available in ACR, and select a different target
version to exercise an upgrade. Keep the baseline compatible with the current
Cache CRD and controller chart values. Baseline
installation clears the Make target's image-tag overrides so chart `appVersion`
selects the baseline images and Helm's reset-then-reuse-values upgrade can
deploy the target images.

The `1es-dacs-pool` Linux agents need connectivity to the AKS API, the ACR,
and public tool downloads. Tests running inside the cluster use their per-suite
workload identity and storage account. Agent-side dynamic container creation
and deletion are disabled in the selected perf tests; no runner storage role
or subnet rule is added. Monitoring is disabled for these ephemeral clusters.

#### DACS-specific integration wiring

The pipeline uses the standard provision, prerequisite install, application
install, test, report, and unprovision Make-based lifecycle. Its repository-specific
settings are:

| Setting | Reason |
|---------|--------|
| 1ES unofficial template and DACS agent pool | These are the pipeline framework and agents configured for this repository; artifacts use `templateContext.outputs` |
| Go and Helm installer tasks; `make kubectl` | Tools must be installed on the DACS agents rather than assuming an Azure Linux container with `tdnf` |
| `featureFlags.golang.internalModuleProxy.enabled` | Enables the 1ES internal Go module proxy for Ginkgo installation and test dependencies |
| Selected pipeline release metadata | DACS publishes Git-derived versions, not Azure build-number image tags; each job reads the same selected official or PR run's image and chart versions |
| Explicit chart versions and `dacs/` registry paths | DACS charts and images live beneath `dacs/`; default Make chart-version derivation does not match published SemVer versions |
| Full controller/cache-server repositories and cleared baseline tags | Keep Helm image resolution and upgrade/rollback checks consistent without pinning the baseline image through the upgrade |
| `cacheServer.numServers` for the legacy chart | This repository's legacy chart uses that field rather than `cacheServer.scaling.static.numServers` |
| Per-suite `TEST_PACKAGES` and existing `TEST_OUTPUT` | DACS writes JUnit directly through its suite reporter; selecting one package avoids another package overwriting the report |
| Explicit test metadata | Deployment outputs set `STORAGE_ACCOUNT` and `WORKLOAD_IDENTITY_CLIENT_ID`; `AKS_CLUSTER_NAME` and `AKS_RESOURCE_GROUP` avoid node-RG reads |
| Temporary deployment parameters | Removes placeholder monitoring IDs from DACS profiles for ephemeral clusters |
| Pinned official baseline in the same test ACR | Uses a published DACS chart and images; there is no separate parent release/cross-registry promotion flow |
| Shared-RG deployment and resource-only cleanup | Restricts provisioning to a permanent RG while preserving parallel suites and isolating each attempt |

For a targeted local package run, use `TEST_PACKAGES=./test/aks` with
`make test-e2e`. The default remains all three packages. The pipeline selects
one package per stage so suite report files cannot overwrite one another.

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
