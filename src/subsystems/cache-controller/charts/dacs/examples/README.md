## Cache Examples

The standalone [static](cache/static.yaml) and [dynamic](cache/dynamic.yaml)
manifests define `cache-sample`, including its storage, resources, eviction,
node-selection, and scaling settings.

Apply the static scaling example:

```bash
kubectl apply \
  --namespace cache-server \
  -f src/subsystems/cache-controller/charts/dacs/examples/cache/static.yaml
```

Apply the dynamic scaling example:

```bash
kubectl apply \
  --namespace cache-server \
  -f src/subsystems/cache-controller/charts/dacs/examples/cache/dynamic.yaml
```

Both manifests use AKS integration-test sizing and NVMe host storage. Copy
the desired manifest and edit it directly for environment-specific settings.

For Minikube, use [minikube.yaml](cache/minikube.yaml): two servers with 4 GB
each and no explicit CPU or memory limits.

Install the controller and Cache resources in `cache-server`. If the Helm
release uses another namespace, pass that namespace to the example commands.
For fields not shown here, run
`kubectl explain cache.spec --api-version=storage.azure.com/v1 --recursive`.

Delete the Cache while the controller is still running so its finalizer can
complete:

```bash
kubectl delete \
  --namespace cache-server \
  -f src/subsystems/cache-controller/charts/dacs/examples/cache/static.yaml \
  --ignore-not-found \
  --wait=true
```
