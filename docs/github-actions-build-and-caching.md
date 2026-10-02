# GitHub Actions build and caching

This document explains how the DACS GitHub Actions CI build compiles, packages,
tests, and caches outputs for Linux `amd64` and `arm64`.

GitHub Actions and Azure Pipelines build the same source, but they use different
cache and artifact backends:

- GitHub Actions uses the GitHub Actions cache and workflow artifacts.
- Azure Pipelines uses Azure Pipeline caches, pipeline artifacts, and ACR-backed
  BuildKit caches.

For the Azure implementation, see
[Azure Pipelines build and caching](azure-pipeline-build-and-caching.md).

## End-to-end workflow

`.github/workflows/ci.yml` is the CI entry point. It calls the reusable
`.github/workflows/build.yml` workflow once for each architecture.

```text
                           ci.yml
                             |
                 +-----------+-----------+
                 |                       |
          build(amd64)               build(arm64)
                 |                       |
       reusable build.yml         reusable build.yml

Each reusable workflow runs the same architecture-specific graph:

                  +----------------+
                  |                |
               compile         images-go
                  |
          +-------+-------+
          |               |
      images-cpp        packages

The build jobs publish the native image, build tree, component images, Python
wheel, and Rust crate. The caller then consumes those artifacts:

          +---------------+----------------+
          |               |                |
      unit-test       image-scan      integration-test
```

The GitHub workflow builds locally in each runner. It does not publish
container images or packages to ACR or a package registry.

## Native-build image

The `compile` job builds the native toolchain image directly from:

```text
src/images/native-build/Dockerfile
```

`docker/build-push-action` uses:

```text
push: false
cache-from: type=gha,scope=native-build-<architecture>
cache-to: type=gha,scope=native-build-<architecture>,mode=min
outputs: type=docker,dest=/tmp/dacs-native-build-image-<architecture>.tar
```

The image is tagged with the ACR-shaped name expected by `bldenv.sh`, but it
remains local to the runner:

```text
dacstest.azurecr.io/dacs/native-build:linux-amd64
dacstest.azurecr.io/dacs/native-build:linux-arm64
```

BuildKit writes the Docker archive directly. The `compile` job loads it once
for compilation and uploads the same archive as:

```text
dacs-native-build-image-amd64
dacs-native-build-image-arm64
```

Downstream jobs download and load the exact image. They do not rebuild it,
rerun `docker save`, or depend on private ACR access.

## GitHub Actions BuildKit caches

BuildKit caches are separated by image and architecture:

```text
native-build-amd64
native-build-arm64

cache-server-amd64
cache-server-arm64
cache-test-client-amd64
cache-test-client-arm64

cacheserver-scheduler-amd64
cacheserver-scheduler-arm64
cache-controller-amd64
cache-controller-arm64
```

The cache scopes prevent one architecture or image from overwriting another
image's cache history.

The single-stage native-build and CacheServer images use `mode=min` to avoid
exporting intermediate cache records that no later stage can reuse. The
multi-stage CacheClient, scheduler, and controller images retain `mode=max` so
dependency-download and compiler stages remain reusable.

## Compiler and package caches

The reusable build workflow also uses `actions/cache`.

### ccache

The ccache key includes:

- Runner operating system.
- Runner architecture.
- CMake build type.
- Hashes of the native-build Dockerfile and toolchain file.
- Current source commit.

Restore prefixes first prefer the same toolchain inputs, then any cache for the
same operating system, architecture, and build type. ccache still validates
individual compiler invocations before reusing an object.

### Cargo

The Cargo cache stores:

```text
~/.cache/dacs-cargo
src/subsystems/storage/rust/dacs_client/target
```

Its key includes the runner operating system, architecture, and
`Cargo.lock` hash. The unit-test and packages jobs use the same cache boundary.

## Build-tree and package artifacts

The compile job packages:

```text
bin/
build/**/CTestTestfile.cmake
```

into:

```text
dacs-build-tree-<architecture>
```

Object files and static-library intermediates are intentionally excluded. Test
jobs need the binaries and CTest metadata, not the complete CMake build tree.

The reusable workflow also uploads:

| Artifact pattern | Contents |
| --- | --- |
| `dacs-native-build-image-<arch>` | BuildKit-exported native toolchain image |
| `dacs-build-tree-<arch>` | Native binaries and CTest metadata |
| `dacs-image-<name>-<arch>` | BuildKit-exported component image |
| `dacs-wheel-<arch>` | Python wheel |
| `dacs-rust-crate-<arch>` | Rust crate |

Large Docker and build-tree archives are uploaded with compression disabled.
They are already composed mostly of compressed packages and binaries, so ZIP
recompression adds CPU time without materially reducing transfer size.
Artifacts are scoped to the workflow run and have short retention periods.
They are transport objects between jobs, not releases.

## C++ application images

The `images-cpp` job restores the compiled build tree and stages the native
binaries in the Docker context.

Each complete C++ application image is built directly with
`docker/build-push-action`:

```text
Docker/CacheServer.Dockerfile
Docker/CacheClient.Dockerfile
```

The operating-system packages and runtime dependencies are part of those
Dockerfiles instead of separate DACS base images. Each image uses its own
architecture-specific GitHub Actions BuildKit cache scope.

The workflow uses Docker's `no-cache-filters: runtime` option. This refreshes
Ubuntu packages on every image build while preserving reusable layers from
other stages, including the CacheClient blobfuse2 builder.

Public base images use readable tags pinned to SHA-256 manifest digests. The
digest guarantees that a build uses the reviewed image contents even if the
upstream tag is moved. Dependabot proposes reviewed updates when a newer digest
is available for the configured tag.

The dedicated Notation workflow discovers every MCR base image referenced by a
Dockerfile and verifies its ESRP signature before allowing the workflow to pass.
Verification uses the Microsoft Supply Chain RSA Root CA 2022, the Microsoft
ESRP timestamp root, a strict trust policy, and the expected Microsoft SCD
Products signing identity. An unpinned image, invalid signature, untrusted
signer, or repository missing from the trust policy fails the job. The workflow
downloads the public Microsoft trust roots over HTTPS and verifies their pinned
SHA-256 fingerprints before adding them to Notation's temporary trust store.
Notation itself is installed from the official 1.3.2 release archive using a
pinned SHA-256 checksum because the pinned setup action's built-in release table
does not include that version.

BuildKit exports each resulting image directly as a Docker archive containing
both its local and versioned tags. The archive is uploaded without first
loading the image and running `docker save`.

## Go application images

The scheduler and controller images compile Go inside their own Dockerfiles.
They do not depend on the native build tree, so their matrix jobs can run in
parallel with native compilation.

Each Go image has its own architecture-specific BuildKit cache scope and its
own directly exported image artifact.

## Tests and scans reuse build outputs

Downstream jobs consume artifacts from the build matrix:

- Unit tests restore the build tree and native-build image.
- The combined Python and Rust package job restores each architecture's build
  outputs once and emits both package artifacts.
- Image scans pass one component Docker archive directly to Trivy without
  loading it into Docker.
- Minikube integration tests load the component image artifacts they deploy.

No downstream job should recompile native source merely to run a test.

## Pull request and main behavior

The same workflow runs for pull requests and pushes to `main`.

GitHub Actions concurrency cancels an older in-progress run for the same pull
request. Pushes to `main` use the run ID in the concurrency group, so separate
main pushes are not grouped by branch in this workflow.

Every triggered workflow runs the complete build, unit-test, image-scan, and
Minikube integration-test matrix for both AMD64 and ARM64.

Both pull request and `main` runs can populate GitHub Actions caches. Neither
path publishes the native-build image, application images, Python wheel, or
Rust crate to an external registry.

Fork pull requests receive a read-only token and no repository secrets. The
workflow therefore relies on public inputs, GitHub-managed caches, and
same-run artifacts rather than private ACR credentials.

## Relationship to Azure Pipelines

The systems use parallel concepts:

| Build concern | GitHub Actions | Azure Pipelines |
| --- | --- | --- |
| Native Docker cache | `type=gha`, scoped by architecture | `type=registry`, stored in ACR |
| C++ image Docker cache | `type=gha`, by image/arch | `type=registry`, in ACR |
| Native image between jobs | Saved workflow artifact | Rebuilt/loaded per architecture job |
| Build tree between jobs | Workflow artifact | Pipeline artifact |
| ccache | `actions/cache` | `Cache@2` |
| Cargo cache | `actions/cache` | `Cache@2` |
| Registry publication | None in CI | Build candidates and registry-side promotion |

The common correctness rule is the same: always evaluate current Docker build
inputs, then let BuildKit decide which layers remain valid.

## Troubleshooting

### A downstream job rebuilds the native image

Check that the compile job uploaded
`dacs-native-build-image-<architecture>` and that the downstream restore action
downloaded and loaded it.

The loaded tag must match the architecture-specific tag expected by
`bldenv.sh`.

### A BuildKit cache has a low hit rate

Confirm that the cache scope includes the correct image and architecture.
Inspect the first Dockerfile instruction reported as not cached. A changed
instruction invalidates every dependent layer.

### ccache restores but compilation is still slow

A restored ccache directory does not guarantee that every compiler invocation
matches. Check whether compiler flags, generated headers, source files, the
toolchain image, or build type changed.

### An image scan cannot find its image

Confirm that the expected `dacs-image-<name>-<architecture>` artifact exists
and contains `/tmp/dacs-image-<name>.tar` after download. Trivy scans that
archive directly; the scan job does not run `docker load`.

### Documentation-only changes do not run CI

`ci.yml` ignores `**/*.md` changes for push and pull request triggers. Markdown
validation is handled by the repository's separate documentation and security
workflows.

## Related documentation

- [Azure Pipelines build and caching](azure-pipeline-build-and-caching.md)
- [Build from source](build-from-source.md)
- [Testing](../test/README.md)
