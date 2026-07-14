# Perf Testing

Run upload / download throughput tests on a cluster with cache-servers deployed, with increasing parallelism.

## Usage

Edit the configuration in the script `batch_job_runner.sh` and run it without arguments.
The K8s cluster should have `dacs-regcred` secret configured to authenticate with the ACR for image pull.

### Build and update container

To build and push the PerfClient.cpp container, run:

```bash
# cd <DACS root>
current_date=$(date +%s)
docker build . -f src/subsystems/storage/dacs_client/Dockerfile -t dacs.azurecr.io/storage-perf-client:${current_date}
docker push dacs.azurecr.io/storage-perf-client:${current_date}
```
Make sure to update the reference to the image in the K8s YAML files.

**Performance Metrics:** The PerfClient reports detailed per-chunk latency metrics (p50, p90, p99) in addition to throughput measurements, providing better insights into tail latencies.