# Workload Scenario Performance Tests

Scenario-based performance tests that measure real workload throughput against DACS.
The first scenario is **AI model loading** — reading large model weight files from Azure Blob
Storage through different access methods, with and without the DACS cache.

## Architecture

```
test/perf/scenario_model_loading_test.go   ← Ginkgo test: method × cache mode matrix
test/pkg/scenario/
  types.go              ← Core types (ScaleProfile, AccessMethod, CacheMode, result structs)
  runner.go             ← Job orchestrator: builds K8s jobs, injects Python, extracts results
  provision.go          ← Data provisioning: mirrors HF models to Azure Blob
  cache_evict.go        ← Cache eviction via native protobuf protocol
  cachepb/cache.pb.go   ← Generated Go protobuf for cache server communication
  workloads/
    harness.py              ← Common entry point: shard discovery, timing, JSON output
    method_blobfile.py      ← Azure blobfile SDK (no_cache: direct blob; cache modes: dacs_blobfile_patcher)
    method_python_adapter.py← DACSCacheClient (direct async API)
    method_interceptor.py   ← LD_PRELOAD storage_intercept (transparent interception)
    method_hf_downloader.py ← Hugging Face hub download API
    provision.py            ← Python provisioning: HF snapshot → blob upload
```

### How it works

1. **Provisioning** — A K8s job runs `provision.py` which downloads the model from
   Hugging Face (if not already in blob) and uploads safetensor shards to Azure Blob Storage.
   Most access methods read from this blob data. The exception is `hf_downloader`, which
   reads directly from Hugging Face Hub as a baseline comparison.

2. **Test execution** — For each `(method, cache_mode)` combination, the Go runner creates
   a K8s job that embeds the Python workload scripts via heredocs. The job reads all model
   shards and emits a JSON result with timing and throughput metrics.

3. **Cache management** — For `cold_cache`, the cache group is evicted before the run.
   For `warm_cache`, a cold load is performed first to populate the cache, then the
   measured run reads from warm cache.

4. **Result publishing** — Results are always printed to stdout. When `SEND_METRICS=true`
   (set automatically in CI), results are also published to EventHub/Kusto.

## Access Methods

| Method | Status | Cache Support | Description |
|--------|--------|---------------|-------------|
| `blobfile` | ✅ Implemented | all modes | Azure `blobfile` SDK; cache modes use `dacs_blobfile_patcher` |
| `python_adapter` | ✅ Implemented | all modes | `DACSCacheClient` — DACS Python client (direct async API) |
| `interceptor` | ✅ Implemented | all modes | `libStorageIntercept.so` via LD_PRELOAD |
| `hf_downloader` | ✅ Implemented | no_cache only | Hugging Face `huggingface_hub` download API |
| `boosted_blob` | ⏳ Placeholder | — | Boosted blob access (pending) |
| `runai_model_streamer` | ⏳ Placeholder | — | Run:AI Model Streamer (pending) |
| `stream_client` | ⏳ Placeholder | — | Native C++ DACS stream client (pending) |

## Cache Modes

| Mode | Behavior |
|------|----------|
| `no_cache` | Direct access, DACS cache bypassed |
| `cold_cache` | Cache evicted before run — measures cache-miss + populate path |
| `warm_cache` | Cache populated first — measures cache-hit path |

## Scale Profiles

Tests use Hugging Face models, automatically mirrored to Azure Blob.
The 1B and 7B models are public; the 13B and 70B models are gated
(require accepting license terms and a HF token for initial provisioning):

| Scale | Model | Approximate Size |
|-------|-------|-----------------|
| `1B`  | `microsoft/phi-1_5` | ~3 GB |
| `7B`  | `mistralai/Mistral-7B-v0.1` | ~14 GB |
| `13B` | `NousResearch/Llama-2-13b-hf` | ~26 GB |
| `70B` | `NousResearch/Llama-2-70b-hf` | ~140 GB |

## Running the Tests

### Prerequisites

- A Kubernetes cluster with DACS cache servers deployed
- `kubectl` configured to the target cluster
- Azure Storage account with `Storage Blob Data Contributor` role assigned
- Ginkgo installed (`make ginkgo`)

### Ad-hoc CLI Usage

Run a single method at a specific scale:

```sh
# Blobfile, no cache, 1B scale
ginkgo -v -r --timeout=60m \
  --label-filter="scenario && model-loading" \
  ./test/perf -- \
  --storage-account=<ACCOUNT> \
  --scenario-scale=1B \
  --scenario-methods=blobfile \
  --scenario-cache-modes=no_cache
```

Run multiple methods and cache modes:

```sh
# Interceptor with all cache modes at 7B scale
ginkgo -v -r --timeout=60m \
  --label-filter="scenario && model-loading" \
  ./test/perf -- \
  --storage-account=<ACCOUNT> \
  --scenario-scale=7B \
  --scenario-methods=interceptor \
  --scenario-cache-modes=no_cache,cold_cache,warm_cache
```

Run the full matrix:

```sh
ginkgo -v -r --timeout=120m \
  --label-filter="scenario && model-loading" \
  ./test/perf -- \
  --storage-account=<ACCOUNT> \
  --scenario-scale=7B \
  --scenario-methods=blobfile,python_adapter,interceptor,hf_downloader \
  --scenario-cache-modes=no_cache,cold_cache,warm_cache
```

### Via Make

```sh
make test-e2e TEST_FILTER="scenario && model-loading" \
  EXTRA_TEST_ARGS="--scenario-scale=7B --scenario-methods=interceptor --scenario-cache-modes=no_cache,cold_cache,warm_cache"
```

### Result Publishing

By default, results are printed to stdout but **not** sent to EventHub. To enable publishing:

```sh
export SEND_METRICS=true
```

This is set automatically in the CI pipeline (`Create-Cacheserver-AKS-Test.yml`).

## CLI Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--scenario-scale` | `7B` | Model scale: `1B`, `7B`, `13B`, `70B` |
| `--scenario-methods` | `blobfile` | Comma-separated access methods to test |
| `--scenario-cache-modes` | `no_cache` | Comma-separated cache modes to test |
| `--storage-account` | (required) | Azure Storage account name |
| `--storage-container` | `test-data` | Azure Storage container name |

## CI Integration

The scenario tests run as part of the existing `Create-Cacheserver-AKS-Test.yml` pipeline,
which runs twice daily. Tests are selected via the `scenario` Ginkgo label (separate from
`perf` which runs only the perf-client benchmarks). The pipeline exposes three parameters
(`scenarioScale`, `scenarioMethods`, `scenarioCacheModes`) that map to the `--scenario-*`
CLI flags.

## Regenerating Protobuf

The cache server protocol uses a custom binary format (4-byte big-endian length prefix +
protobuf body). The Go protobuf types in `cachepb/cache.pb.go` are generated from the
C++ source of truth at `src/subsystems/storage/common/proto/cache.proto`.

**When to regenerate:** After any changes to `cache.proto` (e.g. new request types, field
additions).

**How to regenerate:**

```sh
# From the test/pkg/scenario/ directory:
go generate ./...
```

Or equivalently, from the repo root:

```sh
cd test/pkg/scenario
go generate ./...
```

**Requirements:** `protoc` (v4.23+) and `protoc-gen-go` must be on `$PATH`:

```sh
go install google.golang.org/protobuf/cmd/protoc-gen-go@latest
```

## Adding a New Access Method

1. Create `test/pkg/scenario/workloads/method_<name>.py` implementing `load_shard(path, size)`.
   The function should read the entire shard and return bytes read.
2. Add the method constant to `types.go`.
3. Add the runner case in `runner.go` to inject the script and set up the environment.
4. Update `isValidCombination()` in `scenario_model_loading_test.go` for supported cache modes.
5. Remove the corresponding `PIt` placeholder entries.

## Adding a New Scenario

The framework is designed to support additional scenarios beyond model loading:

1. Create a new test file `test/perf/scenario_<name>_test.go`.
2. Define scenario-specific types and a runner in `test/pkg/scenario/`.
3. Add workload scripts in `test/pkg/scenario/workloads/`.
4. Wire up result publishing with a new `kusto.ScenarioResult` variant if needed.
