## Deploy DACS on AKS in 10 minutes

By the end of this tutorial, you will have installed the DACS controller,
created one CacheServer with ephemeral storage, and verified an upload and
download through cache discovery.

The published OCI charts and images at `dacstest.azurecr.io` are publicly readable
and can be pulled anonymously.

The 10-minute estimate starts after cluster creation. It assumes:

- An existing AKS cluster and a working `kubectl` context for it
- Helm 3
- Linux nodes with enough free CPU, memory, and ephemeral storage

This configuration is for evaluation only. Cache data is lost when the
CacheServer pod or release is removed. For NVMe/RAID storage, production sizing,
scaling, authentication, troubleshooting, and uninstall details, see the
[deployment user guide](../user-guide.md).

## Set variables

Run all commands in the same shell. Set the namespace:

```bash
export NAMESPACE="dacs-system"
```

Confirm that `kubectl` targets the intended cluster:

```bash
kubectl config current-context
kubectl cluster-info
```

## Resolve matching chart and image versions

Helm resolves the latest available semantic version when `--version` is omitted.
Read that chart's metadata first so the smoke-test client uses the matching
`appVersion`, which is the published image build number:

```bash
export CHART_REF="oci://dacstest.azurecr.io/acstor/charts/dacs"
CHART_METADATA=$(helm show chart "$CHART_REF")

export CHART_VERSION=$(
  printf '%s\n' "$CHART_METADATA" |
    awk '$1 == "version:" {gsub(/["\047]/, "", $2); print $2; exit}'
)
export APP_VERSION=$(
  printf '%s\n' "$CHART_METADATA" |
    awk '$1 == "appVersion:" {gsub(/["\047]/, "", $2); print $2; exit}'
)

if [ -z "$CHART_VERSION" ] || [ -z "$APP_VERSION" ]; then
  echo "Could not resolve chart version and appVersion" >&2
  exit 1
fi

printf 'Chart version: %s\nApp version: %s\n' \
  "$CHART_VERSION" "$APP_VERSION"
```

Checkpoint: both values are non-empty. Keep them in the same shell for the
remaining steps.

## Install DACS and an ephemeral cache

The chart creates a single 2 GiB CacheServer on a Linux node. No prerequisite
chart, local NVMe disk, or RAID setup is required.

```bash
helm upgrade --install dacs "$CHART_REF" \
  --version "$CHART_VERSION" \
  --namespace "$NAMESPACE" \
  --create-namespace \
  --atomic \
  --wait \
  --timeout 10m \
  --set manager.image.repository=dacstest.azurecr.io/cache-controller \
  --set manager.cacheServerImage.repository=dacstest.azurecr.io/cache-server \
  --set cache.enabled=true \
  --set cache.name=cache-sample \
  --set cache.scaling.static.numServers=1 \
  --set cache.serverSizeInGB=2 \
  --set cache.serverThreads=4 \
  --set cache.storage.ephemeral=true \
  --set cache.nodeSelectorKey=kubernetes.io/os \
  --set cache.nodeSelectorValue=linux
```

The chart uses its `appVersion` for the controller and CacheServer image tags.

Checkpoint: the Helm command completes with release status `deployed`.

## Verify the deployment

Wait for the controller, display the Cache custom resource (CR), and then wait
for the controller-created CacheServer pod. Pod readiness is the hard readiness
check; this guide does not assume that the Cache CR exposes a condition supported
by `kubectl wait`.

```bash
helm status dacs -n "$NAMESPACE"

kubectl wait \
  --namespace "$NAMESPACE" \
  --for=condition=Available \
  deployment/dacs-manager \
  --timeout=10m

kubectl get cache cache-sample -n "$NAMESPACE" -o wide

kubectl wait \
  --namespace "$NAMESPACE" \
  --for=create \
  pod \
  --selector=dacs.azure.com/cache-name=cache-sample \
  --timeout=10m

kubectl wait \
  --namespace "$NAMESPACE" \
  --for=condition=Ready \
  pod \
  --selector=dacs.azure.com/cache-name=cache-sample \
  --timeout=10m

kubectl get pods \
  -n "$NAMESPACE" \
  --selector=dacs.azure.com/cache-name=cache-sample
kubectl get services cache-sample cache-sample-discovery -n "$NAMESPACE"
```

Checkpoint: the CacheServer pod reports `1/1` Ready, and the
`cache-sample-discovery` service exists.

## Run the end-to-end smoke test

Create an upload Job with a unique key prefix. The client discovers the cache
through `cache-sample-discovery.dacs-system.svc.cluster.local`, uploads 1 GiB,
and stores checksums. Fixed Job names are deleted first so this section can be
rerun.

```bash
export SMOKE_IMAGE="dacstest.azurecr.io/cache-test-client:$APP_VERSION"
export SMOKE_PREFIX="quickstart-$(date +%s)"

kubectl delete job dacs-smoke-upload dacs-smoke-download \
  -n "$NAMESPACE" \
  --ignore-not-found

kubectl create job dacs-smoke-upload \
  -n "$NAMESPACE" \
  --image="$SMOKE_IMAGE" \
  -- \
  /app/PerfClient \
  --server-port 9065 \
  --ib-enabled false \
  --is-discovery-enabled true \
  --discovery-endpoint \
    "cache-sample-discovery.${NAMESPACE}.svc.cluster.local" \
  --connection-pool-size 1 \
  --num-threads 1 \
  --data-size-GiB 1 \
  --prefix "$SMOKE_PREFIX" \
  --mode upload \
  --num-iterations 1 \
  --enable-calculate-checksum true

kubectl wait \
  --namespace "$NAMESPACE" \
  --for=condition=Complete \
  job/dacs-smoke-upload \
  --timeout=10m
kubectl logs job/dacs-smoke-upload -n "$NAMESPACE"
```

Create a download Job with the same prefix and checksum validation enabled:

```bash
kubectl create job dacs-smoke-download \
  -n "$NAMESPACE" \
  --image="$SMOKE_IMAGE" \
  -- \
  /app/PerfClient \
  --server-port 9065 \
  --ib-enabled false \
  --is-discovery-enabled true \
  --discovery-endpoint \
    "cache-sample-discovery.${NAMESPACE}.svc.cluster.local" \
  --connection-pool-size 1 \
  --num-threads 1 \
  --data-size-GiB 1 \
  --prefix "$SMOKE_PREFIX" \
  --mode download \
  --num-iterations 1 \
  --enable-validate-checksum true

kubectl wait \
  --namespace "$NAMESPACE" \
  --for=condition=Complete \
  job/dacs-smoke-download \
  --timeout=10m
kubectl logs job/dacs-smoke-download -n "$NAMESPACE"
```

## Confirm success

The tutorial succeeded when:

- `helm status dacs -n "$NAMESPACE"` reports a deployed release.
- The single CacheServer pod reports `1/1` Ready.
- Both Jobs report `Complete` and their logs contain
  `All iterations succeeded: true`.
- The download Job exits successfully with `--enable-validate-checksum true`
  and does not report a checksum mismatch.

## Clean up

Delete the smoke-test Jobs and Helm release:

```bash
kubectl delete job dacs-smoke-upload dacs-smoke-download \
  -n "$NAMESPACE" \
  --ignore-not-found
helm uninstall dacs -n "$NAMESPACE"
```

The ephemeral cache data disappears when its pod or the release is removed.
Delete the namespace too if it is no longer needed:

```bash
kubectl delete namespace "$NAMESPACE"
```

For a production deployment using local NVMe/RAID, continue with the
[deployment user guide](../user-guide.md). Contributors who want to compile
DACS and build images locally should use [Build from source](build-from-source.md).
