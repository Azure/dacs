# Custom image registries

Set registry configuration at the top level of the `dacs` or legacy
`cache-server` parent chart. With `cacheServerPrereq.enabled: true`, Helm passes
the global values to the bundled prerequisite chart. Its component overrides
belong under `cacheServerPrereq`, for example
`cacheServerPrereq.rolloutCheck.image.registry`.

The prerequisite chart can still be installed directly for development; use
the same global values and omit the `cacheServerPrereq` prefix in that case.
Helm does not share values between separate releases.
Registry selection and authentication are independent.
When nodes already have pull access through a credential provider or managed
identity, no image pull Secret is required:

```yaml
cacheServerPrereq:
  enabled: true
global:
  registry: registry.example.com
  registryMode: replace
  imagePullSecrets: []
```

For a registry requiring explicit credentials instead, reference existing
namespace-local pull Secrets:

```yaml
cacheServerPrereq:
  enabled: true
global:
  registry: harbor.example:8443/dacs
  registryMode: replace
  imagePullSecrets:
    - name: registry-auth
```

Per-image registries (in either mode) and `global.registry` (in `replace` mode)
accept a host, optional port, and optional project prefix,
for example `harbor.example:8443/dacs`. Do not include `https://`. A trailing
slash is optional. With no registry specified, existing image references are unchanged.

## Compatibility and upgrades

`global.registryMode` defaults to `legacy`. An upgrade that keeps the existing
image values and does not set a per-image registry preserves both the old prefix
semantics and the old image coverage. The mode controls only the global setting:

| Mode | Controller and generated CacheServer | Cleanup, prerequisite, legacy server/scheduler |
| --- | --- | --- |
| `legacy` (default) | Prefix `global.registry` to the complete repository | Ignore `global.registry`; use component repository/tag |
| `replace` | Replace source host with `global.registry` | Replace source host with `global.registry` |

The table applies when no per-image registry is set. A nonempty per-image
registry always takes precedence and replaces the source host for that image,
regardless of mode.

For example, with `global.registry: mirror/archive` and repository
`source.example/cache-controller`, legacy mode renders
`mirror/archive/source.example/cache-controller:<tag>`, whereas replace mode
renders `mirror/archive/cache-controller:<tag>`. Both are valid mirror layouts.
Legacy mode also preserves the literal prefix, including any trailing slash.

Set `registryMode: replace` only after mirroring **all** required images at the
replacement paths, including cleanup and prerequisite images. The mode does not
copy images or migrate repositories. Review rendered references before upgrading.
Pull-secret configuration is independent and works in either mode.
Empty or omitted per-image registries leave the global setting's mode-dependent
behavior unchanged. An explicit per-image override affects only that image; it
does not enable global replacement for any other image.

This preserves image-resolution semantics, not every chart lifecycle behavior:
current parent charts bundle prerequisites and the controller chart no longer
creates a Cache CR. Follow the parent chart's upgrade/migration instructions and
apply standalone Cache manifests after the controller/prerequisite release
succeeds. Do not enable the bundled prerequisite on top of an existing standalone
release without following its ownership migration guidance.

## Mirror layout and component overrides

Per-image overrides, and global overrides in `replace` mode, **replace the source
registry host**, preserving repository paths.
A source host is a first path segment containing `.` or `:`, or `localhost`.
Thus both `cache-controller` and `source.example.com/cache-controller`
become `registry.example.com/cache-controller`;
`source.example.com/team/cache-controller` becomes
`registry.example.com/team/cache-controller`. A prefix in the target is
prepended to that repository path, not substituted for it.

| Surface | Mirrored reference (source-chart default tags shown) |
| --- | --- |
| Controller | `registry.example.com/dacs/cache-controller:<appVersion>` |
| Controller-generated CacheServer | `registry.example.com/dacs/cache-server:<appVersion>` |
| Pre-delete cleanup Job | `registry.example.com/oss/v2/kubernetes/kubectl:<cleanup.image.tag>` |
| Prerequisite init container | `registry.example.com/cache-setup:<appVersion>` |
| Prerequisite pause container | `registry.example.com/azurelinux/base/core:3.0` |
| Prerequisite rollout-check Job | `registry.example.com/oss/v2/kubernetes/kubectl:<rolloutCheck.image.tag>` |
| Legacy scheduler | `registry.example.com/acstor/dacs-scheduler:<appVersion>` |
| Legacy CacheServer | `registry.example.com/acstor/dacs-server:<appVersion>` |

Mirror **all** required images, layers, and architectures using your registry's
approved copy/import process. Inspect `helm show values` for the actual packaged
release: published application repositories and versions differ from source
defaults. Preserve their repository paths, or set component repositories to
match your mirror layout. Verify destination image contents after transfer.

Every image object retains its existing `repository` and `tag` settings and
accepts an optional `registry`, empty by default. Resolution follows this order:

1. A nonempty per-image `registry` replaces the source host in either mode.
2. Otherwise, `global.registry` follows `registryMode` and the coverage above.
3. With no applicable registry override, the repository remains unchanged.

Both override levels replace an existing source host and preserve repository
subpaths and tags when replacement is selected. A fully qualified `repository`
alone does not opt out of an applicable global override, since published defaults
are themselves fully qualified.

For a global destination with selected exceptions:

```yaml
global:
  registry: registry.example.com
  registryMode: replace
cleanup:
  image:
    registry: tools.example:5000
```

Application images use `registry.example.com`, while cleanup uses
`tools.example:5000/oss/v2/kubernetes/kubectl:<tag>`. A component override also
works with an empty global registry or in `legacy` mode, without changing other
images.
To retain the original MCR location for an image while localizing the rest,
explicitly set that image's `registry: mcr.microsoft.com`.

Alternatively, leave both registry overrides empty and supply fully qualified
component repositories individually, as before. Registry overrides select image
locations only; they do not grant access to any of the registries.

Image objects are `manager.image`, `manager.cacheServerImage`, `cleanup.image`,
`nvmeRaidSetup.image`, `pauseContainer.image`, `rolloutCheck.image`, `cacheServer.image`, and
`cacheServer.scheduler.image` in their respective charts. Empty application
tags retain the existing fallback to the chart's `appVersion`. In a parent
release, prefix the three prerequisite image objects with `cacheServerPrereq.`.

Publishing stamps application repositories without enabling replacement mode,
including `cache-setup` in the bundled prerequisite. Published application images
use the `dacs/` repository prefix. The two parent charts and the standalone prerequisite chart
are published; each parent carries its prerequisite dependency. Inspect the
actual package defaults and use these overrides when deploying to a different
registry.

## Authentication and namespace scope

`global.imagePullSecrets` is an optional list of Kubernetes
`LocalObjectReference` objects containing a valid Secret `name`. It contains no
credentials. Create each named pull Secret **in every workload namespace**:
the controller/cleanup namespace, prerequisite namespace, and every Cache
namespace. The controller copies references to generated StatefulSets, not
Secret contents, and does not create or copy Secrets between namespaces.
Keep the Secrets available until the pre-delete cleanup hook completes.

The default empty list preserves kubelet credential-provider/managed-identity
authentication. For ACR, authorize the AKS kubelet identity to pull from the
target registry using its appropriate registry permissions. Use pull Secrets
when the target registry cannot use that identity, or when your deployment
requires explicit registry credentials. Merely setting a registry hostname does
not configure identity, registry authorization, DNS, or network connectivity.

For standard ACR RBAC mode, AKS-ACR integration grants
`AcrPull` to the kubelet identity. For ABAC-enabled registries, assign
`Container Registry Repository Reader` with the appropriate repository access
instead; `--attach-acr` is not supported for that mode. See
[AKS-ACR integration](https://learn.microsoft.com/en-us/azure/aks/cluster-container-registry-integration).
This does not imply automatic managed-identity authentication across different
clouds or tenants.

For a controller deployed without Helm, `imagePullSecrets: [registry-auth]` in
its configuration or `CACHE_CONTROLLER_IMAGEPULLSECRETS=registry-auth,backup`
sets generated StatefulSet references. Restart the controller after changing
configuration; reconciliation replaces or removes the old references. Helm
updates the controller pod template when these values change.

These are independent authorization mechanisms:

* **Image pulls:** kubelet identity/registry credentials and `imagePullSecrets`.
* **Blob data access:** application identity/SAS and Blob authorization/RBAC.
  Registry values do not grant Blob access or select cloud storage endpoints.
* **Kubernetes API access:** controller, prerequisite, and cleanup ServiceAccount
  RBAC. Registry credentials do not grant API permissions.

## Boundaries and qualification

Application/client workloads are not owned by these charts. Set their mirrored
images and any required pull-secret references separately; node-level
managed-identity authentication remains available to those workloads too.
For test clients, pass
`--client-image=<registry>/cache-test-client:<release>` through `EXTRA_TEST_ARGS`.
External cert-manager or monitoring operators also need their own image settings.

**This is not full disconnected-install qualification.** The prerequisite's
`cache-setup` image includes its disk-management tools and must also be mirrored.
Registry overrides do not mirror build-time package repositories or change RAID
setup, node provisioning, or other deployment dependencies.

Chart/controller localization and optional pull-secret references address only
image configuration and registry authentication.
Supported client-image distribution, a release inventory of digests/source
versions/patches/checksums, and live Azure China plus generic-registry deployment,
upgrade, and uninstall qualification remain separate work. This configuration
does not establish completion of those acceptance criteria.

Chart tests run separately from ordinary Go unit tests. `make test-charts`
installs Helm and pinned `helm-unittest` 1.2.1 into repository-local `.local`
directories, without changing user-wide Helm plugins or configuration. It runs:

* YAML rendering assertions beside each chart in `tests/*_test.yaml`, including
  registry modes, image references, pull secrets, environment order, and controller tolerations.
* Shared registry/pull-secret schema rejection cases in
  `src/subsystems/cache-controller/charts/tests/registry_schema_test.yaml`.
* Go integration tests for publisher/package behavior, bundled prerequisite and
  cleanup hooks, and compatibility with reused values and numeric `--set` tags,
  invoking the pinned Helm CLI. Dependency builds use local file-based charts.

Schema validation is enabled in `helm-unittest`. Test suites are excluded from
published chart packages. Ordinary Go unit tests require neither Helm nor the
plugin:

```bash
make test-charts                    # Containerized, like other unit-test targets
make test-charts USE_CONTAINER=0    # Local toolchain
make test-charts USE_CONTAINER=0 TEST=TestImageRegistryReusedValues
# TEST filters the Go integration cases; YAML suites always run.
cd src/subsystems/cache-controller
GOWORK=off CACHE_CONTROLLER_CONFIG_PATH="$PWD/internal/config/cache-controller-config.yaml" \
  go test ./internal/config ./internal/controller \
  -run 'Test(ImagePullSecrets|ReconcileImageRegistry|RenderTemplate)'
```

In an authorized cluster installed with `global.registryMode=replace`, mirrored
images, and either kubelet identity permissions or namespace-local pull Secrets:

```bash
EXPECTED_IMAGE_REGISTRY=registry.example.com \
make test-e2e TEST_FILTER=controller FOCUS="Private image registry" \
  EXTRA_TEST_ARGS="--client-image=registry.example.com/cache-test-client:<release>"
```

This opt-in test creates a Cache and checks actual generated Pod image references,
pull-secret references, pulled image IDs, and readiness. Test with both an ACR
endpoint and a generic private registry as required by your environment.
Also verify controller/prerequisite readiness, upgrade with a mirrored release,
and uninstall while credentials remain available, checking cleanup Job success
and CacheServer deletion. Rendering and fake-client tests do **not** prove
authenticated image pulls, upgrade availability, or successful cleanup.
