# Azure Pipelines build and caching

This document explains how the DACS Azure Pipelines build produces native
binaries, packages, and container images for Linux `amd64` and `arm64`. It also
explains which caches improve performance, which registry tags are durable, and
when official builds update rolling build images.

The design has two priorities:

1. Build inputs must always be evaluated so a stale image cannot hide a
   dependency or Dockerfile change.
2. Unchanged work should be restored from architecture-specific caches instead
   of being rebuilt.

For the GitHub Actions implementation of the same build, see
[GitHub Actions build and caching](github-actions-build-and-caching.md).

## End-to-end pipeline flow

Both Azure Pipeline entry points use
`.pipelines/templates/Dacs.Pipeline.Stages.yml`:

- `.pipelines/dacs-official.yml` runs for `main` and scheduled official builds.
- `.pipelines/dacs-pullrequest.yml` runs pull request validation.

The official pipeline enables native-build image publication. Pull request
validation leaves it disabled.

```text
                         dacs-official or dacs-pullrequest
                                      |
                    +-----------------+-----------------+
                    |                                   |
             Build_amd64                         Build_arm64
                    |                                   |
        Build/load native-build image        Build/load native-build image
        Compile C++ and Go                    Compile C++ and Go
        Build Python wheel                    Build Python wheel
        Build Rust crate                      Build Rust crate
        Package build artifacts               Package build artifacts
        Official only: push rolling tag       Official only: push rolling tag
        dacs/native-build:linux-amd64          dacs/native-build:linux-arm64
                    |                                   |
          +---------+---------+               +---------+---------+
          |                   |               |                   |
   Images_amd64       AzureTests_amd64  Images_arm64       AzureTests_arm64
          |                                   |
   Build application images            Build application images
   Package image tarballs               Package image tarballs
          |                                   |
          +-----------------+-----------------+
                            |
                         Package
                            |
                  Package Helm charts and
                     release metadata
                            |
         +------------------+------------------+
         |                                     |
 AzureTests_amd64 and arm64               Package succeeded
         |                                     |
         +------------------+------------------+
                            |
                      PublishImages
                            |
               Push immutable application
                 images and Helm charts
```

Official architecture jobs push their rolling native-build tag after
compilation and package creation. The tags are not gated on later test,
application-image publication, or Helm publication stages.

## Native-build image lifecycle

The native-build image is the toolchain environment used by `bldenv.sh`. It
contains Clang, CMake, Ninja, native dependencies, Python tooling, and other
build prerequisites from `src/images/native-build/`.

Each architecture build calls:

```text
.pipelines/scripts/build/rebuild-native-build-image.sh
```

The script always asks BuildKit to evaluate the current Dockerfile and build
context. It does not use a Git branch comparison to decide whether to build.
This is important for official `main` builds, where `origin/main` and `HEAD`
normally identify the same commit and therefore cannot reveal whether the
existing rolling image is older.

The BuildKit invocation uses:

```text
builder image:
mcr.microsoft.com/oss/v2/moby/buildkit:v0.31.0

--pull
--platform linux/<architecture>
--cache-from type=registry,ref=<native cache reference>
--cache-to type=registry,ref=<native cache reference>,mode=max
--load
```

`--load` places the evaluated image in the job's local Docker daemon under the
tag expected by `bldenv.sh`:

```text
dacstest.azurecr.io/dacs/native-build:linux-amd64
dacstest.azurecr.io/dacs/native-build:linux-arm64
```

These local tags are used for compilation. They are not automatically pushed
just because the image was built.

The builder image is pulled from Microsoft Container Registry instead of
Docker Hub. This keeps Azure builds independent of Docker Hub availability and
rate limits. The pipeline pins the multi-architecture manifest by digest.

## Native-build cache references

BuildKit stores intermediate and final layers in architecture-specific ACR
cache references:

```text
dacstest.azurecr.io/dacs/native-build:buildcache-linux-amd64
dacstest.azurecr.io/dacs/native-build:buildcache-linux-arm64
```

These references are build caches, not release images. Consumers should not
run or deploy them.

BuildKit calculates cache validity from Dockerfile instructions, copied files,
build arguments, base images, and the build context. If the fmt dependency
definition changes, for example, the affected dependency layer is rebuilt
while unrelated earlier layers can still be restored.

## Rolling native-build tags

After each official architecture job compiles the source and creates its build
artifacts, it directly pushes:

```text
dacstest.azurecr.io/dacs/native-build:linux-amd64
dacstest.azurecr.io/dacs/native-build:linux-arm64
```

There is no candidate or promotion phase. A later test or publication failure
does not roll back the native-build tag. Pull request builds never push these
rolling tags.

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
architecture-specific BuildKit cache references for the complete images:

```text
dacstest.azurecr.io/cache-server:buildcache-linux-amd64
dacstest.azurecr.io/cache-server:buildcache-linux-arm64
dacstest.azurecr.io/cache-test-client:buildcache-linux-amd64
dacstest.azurecr.io/cache-test-client:buildcache-linux-arm64
```

These references contain reusable build layers only. They are not deployment
tags and are not published as application images.

Public base images use readable tags pinned to SHA-256 manifest digests. The
digest guarantees that a build uses the reviewed image contents even if the
upstream tag is moved. Dependabot proposes reviewed updates when a newer digest
is available for the configured tag.

Before either architecture build starts, the `VerifyBaseImages` stage discovers
every MCR base image referenced by a Dockerfile and validates its ESRP signature
with the built-in Azure Pipelines `Notation@0` task. The stage downloads the
public Microsoft supply-chain and timestamp roots over HTTPS, verifies their
pinned SHA-256 fingerprints, and applies the same strict trust policy used by
GitHub Actions. An unpinned image, invalid signature, untrusted signer, or
repository missing from the trust policy fails the pipeline.

The Go scheduler and controller images compile Go in their own Dockerfiles.

## Pipeline artifacts and registry objects

Pipeline artifacts move outputs between jobs without rebuilding them:

| Artifact | Producer | Consumers |
| --- | --- | --- |
| `dacs-build-tree-linux-<arch>` | Architecture build job | Tests and C++ image jobs |
| `dacs-python-wheel-linux-<arch>` | Architecture build job | Package consumers |
| `dacs-rust-crate-linux-<arch>` | Architecture build job | Package consumers |
| `container-images-linux-<arch>` | Architecture image job | `PublishImages` |
| `helm-charts` | `Package` | `PublishImages` |
| `release-metadata` | `Package` | `PublishImages` |

Container image tarballs are checksummed before publication. `PublishImages`
loads the tarballs and pushes immutable application image tags. Helm charts
are also published only after their checksums and metadata are validated.

Registry objects serve different purposes:

| Object | Mutable | Intended use |
| --- | --- | --- |
| `buildcache-linux-<arch>` | Yes | Native and C++ application BuildKit layer caches |
| `dacs/native-build:linux-<arch>` | Yes | Rolling toolchain image used by local builds |
| Application image version tags | No | Release and deployment inputs |

## Pull request and official build behavior

Both pipeline types can read and update BuildKit cache references after ACR
authentication. Cache writes improve later builds but do not change a
deployable or rolling image tag.

Only `dacs-official` sets:

```yaml
pushNativeBuildImages: true
```

That enables direct rolling-tag pushes from the architecture build jobs. The
pull request pipeline uses the default `false` value.

## Relationship to local builds

`make build` uses `bldenv.sh`, which looks for the rolling architecture tag:

```text
dacstest.azurecr.io/dacs/native-build:linux-<host architecture>
```

If the image is not already local, `bldenv.sh` tries to pull it. If the pull is
unavailable, it builds `src/images/native-build/Dockerfile` locally.

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
restored or rebuilt. In Azure Pipelines, the build should always show the
BuildKit build step before compilation. A direct pull-and-exit decision
indicates older pipeline logic.

### A Dockerfile change appears to be ignored

Check the BuildKit output for the affected instruction. A valid cache hit is
reported as cached. A changed `COPY`, `RUN`, build argument, or base image
should invalidate the relevant layer and every dependent layer.

Do not delete the cache merely because many layers are reused. Cache reuse is
expected when BuildKit has verified that the inputs are unchanged.

### The rolling native-build image is stale

Check that:

1. The corresponding official architecture build job succeeded.
2. The `Push linux/<architecture> native-build image` step ran.
3. The rolling tag was pushed to the `dacs/native-build` repository.

Later test or publication stages do not control this push.

### A cache reference is unavailable

BuildKit treats a missing cache as a cold build. Verify ACR authentication and
write permission if cache export fails. Do not replace an unavailable cache
with a stale rolling image; the image must still be evaluated from current
inputs.

## Related documentation

- [GitHub Actions build and caching](github-actions-build-and-caching.md)
- [Build from source](build-from-source.md)
- [Versioning and releases](../VERSIONING.md)
- [Testing](../test/README.md)
