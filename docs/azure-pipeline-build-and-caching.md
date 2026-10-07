# Azure Pipelines build and caching

This document explains how the DACS Azure Pipelines build produces native
binaries, packages, and container images for Linux `amd64` and `arm64`. It also
explains which caches improve performance, which registry tags are durable, and
when official builds update rolling build images.

The design has two priorities:

1. Native-build images must be tied to the exact source commit that produced
   them so a stale rolling image cannot hide a dependency or Dockerfile change.
2. Unchanged work should be restored from architecture-specific caches instead
   of being rebuilt.
3. Large application images should stay in ACR instead of being exported,
   uploaded, downloaded, loaded, and pushed again between pipeline stages.

For the GitHub Actions implementation of the same build, see
[GitHub Actions build and caching](github-actions-build-and-caching.md).

## End-to-end pipeline flow

Both Azure Pipeline entry points use
`.pipelines/templates/Dacs.Pipeline.Stages.yml`:

- `.pipelines/dacs-official.yml` runs for `main` and scheduled official builds.
- `.pipelines/dacs-pullrequest.yml` runs pull request validation.

The [AKS integration pipeline](../.pipelines/dacs-integration.yml) tests
published releases without rebuilding them. See
[integration testing](../test/README.md#azure-pipeline-dacs-integration).

Every architecture build publishes its versioned native-build image for
downstream test jobs. The official pipeline additionally enables rolling-tag
publication; pull request validation leaves rolling-tag publication disabled.

```text
                         dacs-official or dacs-pullrequest
                                      |
                    +-----------------+-----------------+
                    |                                   |
             Build_amd64                         Build_arm64
                    |                                   |
        Verify MCR base images              Verify MCR base images
        Build/load native-build image        Build/load native-build image
        Compile C++ and Go                    Compile C++ and Go
        Build Python wheel                    Build Python wheel
        Build Rust crate                      Build Rust crate
        Package build artifacts               Package build artifacts
        Push exact version tag                Push exact version tag
        Official: push rolling tag            Official: push rolling tag
        native-build:<version>-linux-amd64     native-build:<version>-linux-arm64
        native-build:linux-amd64               native-build:linux-arm64
                    |                                   |
          +---------+---------+               +---------+---------+
          |                   |               |                   |
   Images_amd64       AzureTests_amd64  Images_arm64       AzureTests_arm64
          |                   |               |                   |
   Build application   Sharded storage   Build application   Sharded storage
   images and push     and Python tests   images and push     and Python tests
   candidate tags                        candidate tags
          |                   |               |                   |
          +-------------------+-------+-------+-------------------+
                                      |
                                   Package
                                      |
                        Package and publish Helm charts
                        Promote candidate image manifests
                        Write release metadata
```

Every architecture job pushes its immutable versioned native-build tag
after compilation and package creation. Downstream Azure test jobs select that
exact image. Official architecture jobs also push the rolling native-build tag.
These pushes are not gated on later test, application-image promotion, or Helm
publication steps.

## Native-build image lifecycle

The native-build image is the toolchain environment used by `bldenv.sh`. It
contains Clang, CMake, Ninja, native dependencies, Python tooling, and other
build prerequisites from `src/images/native-build/`.

Each architecture build calls:

```text
.pipelines/scripts/build/rebuild-native-build-image.sh
```

The script first tries to pull the versioned native-build image. When
that image exists, it retags the image locally and skips BuildKit. Otherwise,
BuildKit evaluates the current Dockerfile and build context.

The BuildKit invocation uses:

```text
builder image:
mcr.microsoft.com/oss/v2/moby/buildkit:v0.31.0

--pull
--platform linux/<architecture>
--cache-from type=registry,ref=<native cache reference>
--cache-to type=registry,ref=<native cache reference>,mode=min
--load
```

`--load` places the evaluated image in the job's local Docker daemon under the
tag expected by `bldenv.sh`:

```text
dacstest.azurecr.io/dacs/native-build:linux-amd64
dacstest.azurecr.io/dacs/native-build:linux-arm64
```

These local tags are used for compilation. After compilation, every pipeline
build pushes the versioned tag. Official builds also push the rolling
tag.

The builder image is pulled from Microsoft Container Registry instead of
Docker Hub. This keeps Azure builds independent of Docker Hub availability and
rate limits. The pipeline pins the multi-architecture manifest by digest.

## Native-build cache references

BuildKit stores intermediate and final layers in architecture-specific ACR
cache references. Official builds write the stable `main` cache:

```text
dacstest.azurecr.io/dacs/native-build:buildcache-main-linux-amd64
dacstest.azurecr.io/dacs/native-build:buildcache-main-linux-arm64
```

Pull request builds import the `main` cache and a cache dedicated to the pull
request, but write only the pull-request cache:

```text
dacstest.azurecr.io/dacs/native-build:buildcache-pr-<id>-linux-amd64
dacstest.azurecr.io/dacs/native-build:buildcache-pr-<id>-linux-arm64
```

These references are build caches, not release images. Consumers should not
run or deploy them. The same cache scopes are used for the C++ application
image caches, preventing a pull request from replacing the cache manifest used
by official builds.

Both scopes also import the previous `buildcache-linux-<arch>` reference as a
read-only migration source. No pipeline writes that legacy reference after
cache isolation is enabled.

BuildKit calculates cache validity from Dockerfile instructions, copied files,
build arguments, base images, and the build context. If the fmt dependency
definition changes, for example, the affected dependency layer is rebuilt
while unrelated earlier layers can still be restored.

## Native-build image tags

After each architecture job compiles the source and creates its build
artifacts, it pushes a versioned image:

```text
dacstest.azurecr.io/dacs/native-build:<version>-linux-amd64
dacstest.azurecr.io/dacs/native-build:<version>-linux-arm64
```

Official architecture jobs additionally push the rolling tags:

```text
dacstest.azurecr.io/dacs/native-build:linux-amd64
dacstest.azurecr.io/dacs/native-build:linux-arm64
```

There is no candidate or promotion phase. A later test or publication failure
does not roll back either tag. Pull request builds push only their immutable
versioned tags and never update rolling tags.

## Compiler and package caches

The native image cache avoids rebuilding the toolchain. Separate Azure
Pipeline caches avoid recompiling unchanged source and redownloading packages.

| Cache | Stored data | Key boundary | Restore behavior |
| --- | --- | --- | --- |
| ccache | C/C++ objects | OS, arch, commit | Same OS and arch |
| Cargo dependencies | Registry and Git data | OS, arch, `Cargo.lock` | Same OS and arch |
| Cargo build | Rust target directory | OS, arch, lock, commit | Same OS and arch |
| Native BuildKit | Docker layers | Arch and build inputs | Matching ACR layers |

The caches have different invalidation boundaries on purpose. A source commit
changes the exact ccache and Cargo build keys, while restore prefixes still
allow the tools to reuse entries that remain valid.

## C++ application image caching

The CacheServer and cache test client images are built directly from:

```text
Docker/CacheServer.Dockerfile
Docker/CacheClient.Dockerfile
```

Their operating-system packages and other runtime dependencies are part of
these Dockerfiles rather than separate DACS base images. Azure Pipelines uses
the same official and per-PR cache scopes as the native-build image:

```text
dacstest.azurecr.io/cache-server:buildcache-main-linux-amd64
dacstest.azurecr.io/cache-server:buildcache-pr-<id>-linux-amd64
dacstest.azurecr.io/cache-test-client:buildcache-main-linux-amd64
dacstest.azurecr.io/cache-test-client:buildcache-pr-<id>-linux-amd64
```

Equivalent references exist for `arm64`. These references contain reusable
build layers only. They are not deployment tags and are not published as
application images.

The single-stage native-build and CacheServer images export `mode=min` caches.
That retains the final-stage cache needed for repeated builds without storing
unused intermediate records. The multi-stage CacheClient image keeps
`mode=max` so dependency and builder stages remain reusable.

Both official and pull request image stages use Docker's
`--no-cache-filter runtime` option. This refreshes Ubuntu packages on every
image build while preserving reusable layers from other stages, including the
CacheClient blobfuse2 builder.

Public base images use readable tags pinned to SHA-256 manifest digests. The
digest guarantees that a build uses the reviewed image contents even if the
upstream tag is moved. Dependabot proposes reviewed updates when a newer digest
is available for the configured tag.

At the start of each architecture build, the shared
`Dacs.VerifyBaseImages.Steps.yml` template discovers every MCR base image
referenced by a Dockerfile and validates its ESRP signature with the built-in
Azure Pipelines `Notation@0` task. Running verification on the already
allocated compile agents removes the dedicated verification agent and stage
transition while preserving verification before compilation on both
architectures. The template downloads the public Microsoft supply-chain and
timestamp roots over HTTPS, verifies their pinned SHA-256 fingerprints, and
applies the same strict trust policy used by GitHub Actions. An unpinned image,
invalid signature, untrusted signer, or repository missing from the trust
policy fails the corresponding architecture build.

The Go scheduler and controller images compile Go in their own Dockerfiles.

## Pipeline artifacts and registry objects

Pipeline artifacts move outputs between jobs without rebuilding them:

| Artifact | Producer | Consumers |
| --- | --- | --- |
| `dacs-build-linux-<arch>` | Architecture build job | Tests, C++ image jobs, and package consumers |
| `dacs-release` | `Package` | Build record and release consumers |

The architecture artifact contains `build-tree/`, `python-wheel/`, and
`rust-crate/` subdirectories. Publishing one output avoids repeating 1ES
binary analysis, SBOM generation, and artifact publication for three separate
outputs. The release artifact similarly contains `helm-charts/` and
`release-metadata/` under one output.

Application image jobs do not create pipeline artifacts. Each architecture
pushes the four local images directly to build-specific candidate tags:

```text
<image>:candidate-<Build.BuildId>-linux-amd64
<image>:candidate-<Build.BuildId>-linux-arm64
```

After both image stages and both Azure test stages succeed, the `Package` job
promotes those ACR manifests to immutable
architecture tags and creates the multi-platform tags. Promotion is
registry-side, so the pipeline does not transport approximately 1 GB of Docker
tarballs through the artifact service or run `docker load` before publication.
The same job packages, validates, and publishes Helm charts, removing the
separate publication stage and its agent allocation delay.

Registry objects serve different purposes:

| Object | Mutable | Intended use |
| --- | --- | --- |
| `buildcache-main-linux-<arch>` | Yes | Official native and C++ application BuildKit layer caches |
| `buildcache-pr-<id>-linux-<arch>` | Yes | Pull-request-specific BuildKit layer caches |
| `dacs/native-build:<version>-linux-<arch>` | No | Exact native toolchain image for compilation and downstream tests |
| `dacs/native-build:linux-<arch>` | Yes | Rolling toolchain image used by local builds |
| `candidate-<build-id>-linux-<arch>` | No | Build-specific application image awaiting promotion |
| Application image version tags | No | Release and deployment inputs |

## Pull request and official build behavior

Both pipeline types can read the official `main` BuildKit cache after ACR
authentication. Official builds update that cache. Pull request builds update
only their per-PR cache, so branch-specific Dockerfile changes cannot evict the
official cache manifest.

Whenever `dacs-pullrequest` is queued, it runs the complete Azure-backed test
matrix for both AMD64 and ARM64. Test selection never drops an architecture or
skips suites based on changed paths.

The longest storage suites are sharded within each architecture:

| Script | Jobs |
| --- | --- |
| `test_cache_advanced.sh` | Cache behavior, encryption |
| `test_cache_specialized.sh` | Snapshot, eviction, remote distribution |

`DACS_TEST_GROUP` selects the shard. Its default remains `all`, preserving the
existing local command behavior.

Only `dacs-official` sets:

```yaml
pushNativeBuildImages: true
```

That enables direct rolling-tag pushes from the architecture build jobs. The
pull request pipeline uses the default `false` value, but still publishes the
immutable versioned tags required by its downstream test jobs.

## Relationship to local builds

`make build` uses `bldenv.sh`, which looks for the rolling architecture tag by
default:

```text
dacstest.azurecr.io/dacs/native-build:linux-<host architecture>
```

If the image is not already local, `bldenv.sh` tries to pull it. If the pull is
unavailable, it builds `src/images/native-build/Dockerfile` locally.

Azure test jobs set `NATIVE_BUILD_IMAGE_TAG` to the versioned tag
published by their corresponding architecture build stage. This prevents a
test job on another agent from using a stale rolling image.

`make images` builds the application images locally with normal Docker layer
caching. The Azure Pipeline explicitly enables registry-backed caching for the
C++ application images with:

```text
ENABLE_REGISTRY_BUILD_CACHE=true
```

Local builds leave this disabled, so they do not require permission to write
BuildKit caches to ACR.

## Troubleshooting

### The compiler reports a dependency API that should exist

Confirm which native-build image the job loaded and look for:

```text
Building native-build image for linux/<architecture>
```

Inspect the following BuildKit output to see which Dockerfile layers were
restored or rebuilt. If the exact versioned image already exists, the
job instead reports that it is using the existing native-build image and skips
BuildKit.

### A Dockerfile change appears to be ignored

Check the BuildKit output for the affected instruction. A valid cache hit is
reported as cached. A changed `COPY`, `RUN`, build argument, or base image
should invalidate the relevant layer and every dependent layer.

Do not delete the cache merely because many layers are reused. Cache reuse is
expected when BuildKit has verified that the inputs are unchanged.

### The rolling native-build image is stale

Check that:

1. The corresponding official architecture build job succeeded.
2. The `Push rolling linux/<architecture> native-build image` step ran.
3. The rolling tag was pushed to the `dacs/native-build` repository.

Later test or publication stages do not control this push.

### A cache reference is unavailable

BuildKit treats a missing cache as a cold build. Verify ACR authentication and
write permission if cache export fails. A missing versioned image is
also expected for a new commit; the script then evaluates the current inputs
with BuildKit.

## Related documentation

- [GitHub Actions build and caching](github-actions-build-and-caching.md)
- [Build from source](build-from-source.md)
- [Versioning and releases](../VERSIONING.md)
- [Testing](../test/README.md)
