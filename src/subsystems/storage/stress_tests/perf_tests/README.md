# Perf Testing

Run upload / download throughput tests on a cluster with cache-servers deployed, with increasing parallelism.

## Usage

> **Important:** This workflow is not ready to run externally without
> configuration. `upload_client.yaml` and `download_client.yaml` contain fixed
> `spec.template.spec.containers[].image` values, and
> `batch_job_runner.sh` does not replace them. Publish the image you built to a
> registry accessible from the cluster, then update both fields before
> running. If you use manifests under `IB_Perf/`, update their `image:` fields
> separately.

Edit the configuration in
`src/subsystems/storage/stress_tests/perf_tests/batch_job_runner.sh`, then run
it without arguments from the repository root:

```bash
./src/subsystems/storage/stress_tests/perf_tests/batch_job_runner.sh
```

If the image registry requires authentication, configure an image pull secret
and reference it from the test workload.

### Build and update container

The supported repository image flow packages `PerfClient` in the cache test
client image. From the repository root, run:

```bash
REPOSITORY_ROOT=/path/to/dacs
cd "$REPOSITORY_ROOT"
make build BUILD_TYPE=Release
make images BUILD_TYPE=Release
```

This creates `cache-test-client:local`. Publish or retag it using your
registry's supported workflow, then set the image fields in
`upload_client.yaml` and `download_client.yaml` to the published image.

**Performance Metrics:** The PerfClient reports detailed per-chunk latency metrics (p50, p90, p99) in addition to throughput measurements, providing better insights into tail latencies.