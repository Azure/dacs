# Build from source

Use this guide to build DACS from a clean clone, validate changes, and locate
the resulting native and controller binaries. The supported development
environments are Linux, WSL 2, and the repository devcontainer.

## Prerequisites

For the default containerized native build, install:

- Git and Git LFS
- GNU Make
- Docker Engine or Docker Desktop with at least 16 GB of memory available
- About 50 GB of free disk space for images, dependencies, and build output
- A Linux `amd64` or `arm64` host, or Ubuntu under WSL 2

The Docker daemon must be running, and the current user must be able to run
`docker` without `sudo`.

On WSL 2, enable Docker Desktop's WSL integration and clone into the Linux
filesystem, such as `~/src`, rather than `/mnt/c`.

## Clone the repository

```bash
git clone <repository-url>
cd <repository-directory>
git lfs install
git lfs pull
```

The build downloads dependencies from public package and source repositories.
It may try to reuse a prebuilt native-build image, but falls back to building
that image from `src/images/native-build/Dockerfile`.

## Build the native components

The default build runs CMake and Ninja in the repository's native build
container:

```bash
make build
```

`bldenv.sh` currently starts the build container with elevated host access,
including the Docker socket and host-mounted files. Review that script and only
build source that you trust.

The first build can take several minutes because Docker may need to build the
native build image and compile its dependencies. Later builds reuse the local
image and compiler cache.

Use `BUILD_TYPE` to select a CMake configuration:

```bash
make build BUILD_TYPE=Release
```

The build architecture is detected from the host. It can also be selected
explicitly:

```bash
make build BUILD_ARCH=amd64
make build BUILD_ARCH=arm64
```

## Find the build outputs

Native build output is written under:

```text
bin/<BUILD_TYPE>/<BUILD_ARCH>/native-storage/storage/
```

For example, the default x86-64 debug build writes native binaries and
libraries under `bin/Debug/amd64/native-storage/storage/`.

To remove native build output:

```bash
make clean
```

### Native build behavior

`make build` invokes `.pipelines/scripts/build/build.sh` through `bldenv.sh`.
The script configures the repository root with `CMakeLists.txt`, uses
`toolchain.cmake`, and builds with Ninja. Third-party C++ dependencies are
prepared in `/dependency` when the native build image is created.

`USE_CONTAINER=0` skips `bldenv.sh`, but it still requires the compilers,
tools, libraries, and populated `/dependency` tree supplied by the native
build image. Use this mode inside the devcontainer, not on an otherwise clean
host.

### Embed DACS storage targets in another CMake build

Parent projects can add `src/subsystems/storage` as a subdirectory. Configure
the component options before calling `add_subdirectory`. The parent must
provide the same native dependencies and imported targets as the DACS root
build, so this mode is intended for builds that already use the DACS native
dependency environment:

```cmake
set(DACS_BUILD_CACHE_CLIENT ON)
set(DACS_BUILD_CACHE_SERVER ON)
set(DACS_BUILD_STORAGE_INTERCEPT ON)
set(DACS_BUILD_LIVENESS_PROBE ON)
set(DACS_BUILD_TESTS OFF)
set(DACS_BUILD_TOOLS OFF)

add_subdirectory(
    "${DACS_SOURCE_DIR}/src/subsystems/storage"
    "${CMAKE_BINARY_DIR}/dacs-storage")
```

The embedded build exposes `DACS::CacheProto`, `DACS::Locator`,
`DACS::CacheClient`, `DACS::CacheClientShared`,
`DACS::StorageRemoteClient`, and `DACS::CacheServer`.

### Install and consume the cache client package

Enable `DACS_INSTALL_SDK` to install the cache client libraries, public header,
and CMake package metadata:

```bash
cmake \
  -S . \
  -B build/dacs-sdk \
  -DDACS_BUILD_CACHE_SERVER=OFF \
  -DDACS_BUILD_STORAGE_INTERCEPT=OFF \
  -DDACS_BUILD_LIVENESS_PROBE=OFF \
  -DDACS_BUILD_TESTS=OFF \
  -DDACS_BUILD_TOOLS=OFF \
  -DDACS_INSTALL_SDK=ON \
  -DCMAKE_INSTALL_PREFIX=/path/to/dacs-sdk
cmake --build build/dacs-sdk
cmake --install build/dacs-sdk
```

Consumers can then locate the installed package and link either the shared or
merged static cache client. The package also exports the protobuf and locator
targets used by higher-level clients:

```cmake
find_package(DACS CONFIG REQUIRED COMPONENTS Proto Locator)

target_link_libraries(my_target PRIVATE
    DACS::CacheClientShared
    DACS::CacheProto
    DACS::Locator)
```

Request the `Static` component to use the merged static cache client:

```cmake
find_package(DACS CONFIG REQUIRED COMPONENTS Static)

target_link_libraries(my_target PRIVATE DACS::CacheClient)
```

When `DACS_BUILD_STORAGE_INTERCEPT=ON`, request the `StorageRemote` component
to consume the Blob/remote-storage interface:

```cmake
find_package(DACS CONFIG REQUIRED COMPONENTS StorageRemote)

target_link_libraries(my_target PRIVATE DACS::StorageRemoteClient)
```

The package requires Protobuf and Threads. The `Static` component additionally
requires CURL, OpenSSL, `uuid`, `jsoncpp`, and `xxhash`. Their installation
prefixes must be available through `CMAKE_PREFIX_PATH` or CMake's standard
library search locations.

Set `DACS_DIR` to the directory containing `DACSConfig.cmake`, or add the
installation prefix to `CMAKE_PREFIX_PATH` if it is outside CMake's default
search locations.

## Build the Go cache controller

Outside the devcontainer, install Go 1.25.5 or a newer compatible Go release.
Then run:

```bash
make -C src/subsystems/cache-controller build
```

This command downloads the pinned Go build tools, generates controller code
and manifests, runs formatting and vetting, and builds:

```text
src/subsystems/cache-controller/bin/manager
```

Its runtime configuration is copied to the same `bin` directory.

## Validate changes

Run the smallest test target that covers your change:

```bash
make test-native   # Native C++ unit tests
make test-go       # Go unit tests
make pytest        # Python tests
```

Run all native and Go unit tests before submitting a broad change:

```bash
make test
```

Integration and end-to-end tests require additional services or a Kubernetes
cluster. See [Testing](../test/README.md) for those prerequisites and
workflows.

## Build in the devcontainer

The devcontainer builds from `src/images/native-build/Dockerfile`, so the C++,
CMake, Ninja, Go, and Python dependencies are already installed.

1. Install VS Code, Docker, and the Dev Containers extension.
2. Open the cloned repository in VS Code.
3. Run **Dev Containers: Reopen in Container**.
4. Wait for `.devcontainer/post-create.sh` to finish.
5. Build from the devcontainer terminal:

   ```bash
   make build
   make -C src/subsystems/cache-controller build
   ```

The devcontainer sets `USE_CONTAINER=0`, so `make build` runs directly in the
devcontainer instead of starting a nested native build container. Build
artifacts remain in the clone's `bin` and `build` directories.

## Troubleshooting

### Docker is unavailable

Confirm the daemon is running and accessible:

```bash
docker info
```

On WSL 2, also confirm that Docker Desktop integration is enabled for the
distribution containing the clone.

### A rebuild uses stale native output

Clean the selected configuration and rebuild it:

```bash
make clean BUILD_TYPE=Debug
make build BUILD_TYPE=Debug
```

### Git LFS files contain pointer text

Fetch the repository's LFS content, then rebuild:

```bash
git lfs pull
make clean
make build
```

### The build runs out of memory or disk space

Increase Docker's memory allocation to at least 16 GB and confirm that the
Docker data directory and clone have sufficient free space.

## Related documentation

- [Project overview](../README.md)
- [Deployment user guide](../user-guide.md)
- [Testing](../test/README.md)
