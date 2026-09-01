#!/bin/bash -e

# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Parse arguments
usage="Usage : $0 [--no-tty]"

TTY_OPTION="-t"

# Auto-detect if running without TTY (e.g., in CI/CD)
if [ ! -t 0 ]; then
    TTY_OPTION=""
fi

# Allow explicit --no-tty flag to override
if [ "$1" == "--no-tty" ]; then
    TTY_OPTION=""
    shift
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
WORKSPACE_NAME="$(basename "$DIR")"
CARGO_CACHE_DIR="${CARGO_CACHE_DIR:-$HOME/.cache/dacs-cargo}"
USE_AZURE_CFS="${USE_AZURE_CFS:-false}"
UBUNTU_ARCHIVE_MIRROR="${UBUNTU_ARCHIVE_MIRROR:-}"
if [ "$USE_AZURE_CFS" = "true" ] && [ -z "$UBUNTU_ARCHIVE_MIRROR" ]; then
    UBUNTU_ARCHIVE_MIRROR="http://azure.archive.ubuntu.com/ubuntu"
fi
mkdir -p "$CARGO_CACHE_DIR/registry" "$CARGO_CACHE_DIR/git"

PIP_CONFIG_FILE=""
CARGO_CONFIG_FILE=""
CARGO_CREDENTIALS_FILE=""
cleanup() {
    if [ -n "$PIP_CONFIG_FILE" ]; then
        rm -f "$PIP_CONFIG_FILE"
    fi
    if [ -n "$CARGO_CONFIG_FILE" ]; then
        rm -f "$CARGO_CONFIG_FILE"
    fi
    if [ -n "$CARGO_CREDENTIALS_FILE" ]; then
        rm -f "$CARGO_CREDENTIALS_FILE"
    fi
}
trap cleanup EXIT

pip_runtime_args=()
if [ "$USE_AZURE_CFS" = "true" ] && [ -n "${PIP_INDEX_URL:-}" ]; then
    PIP_CONFIG_FILE="$(mktemp)"
    chmod 600 "$PIP_CONFIG_FILE"
    {
        printf '[global]\n'
        printf 'index-url = %s\n' "$PIP_INDEX_URL"
        if [ -n "${PIP_EXTRA_INDEX_URL:-}" ]; then
            printf 'extra-index-url = %s\n' "$PIP_EXTRA_INDEX_URL"
        fi
    } > "$PIP_CONFIG_FILE"
    pip_runtime_args=(
        -v "$PIP_CONFIG_FILE:/run/secrets/pip.conf:ro"
        --env PIP_CONFIG_FILE=/run/secrets/pip.conf
    )
fi

cargo_runtime_args=()
if [ "$USE_AZURE_CFS" = "true" ]; then
    CARGO_CONFIG_FILE="$(mktemp)"
    chmod 600 "$CARGO_CONFIG_FILE"
    cp "$DIR/.cargo/cfs-config.toml" "$CARGO_CONFIG_FILE"
    cargo_runtime_args=(
        -v "$CARGO_CONFIG_FILE:/opt/cargo/config.toml:ro"
    )

    cargo_registry_token="${CARGO_REGISTRIES_AZURENATIVECONTAINERSTORAGE_PUBLICPACKAGES_TOKEN:-}"
    if [ -n "$cargo_registry_token" ]; then
        CARGO_CREDENTIALS_FILE="$(mktemp)"
        chmod 600 "$CARGO_CREDENTIALS_FILE"
        {
            printf '[registries.AzureNativeContainerStorage_PublicPackages]\n'
            printf 'token = "%s"\n' "$cargo_registry_token"
        } > "$CARGO_CREDENTIALS_FILE"
        cargo_runtime_args+=(
            -v "$CARGO_CREDENTIALS_FILE:/opt/cargo/credentials.toml:ro"
        )
    fi
fi

# Detect host architecture and use architecture-specific image tag
HOST_ARCH=$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')
echo "Detected host architecture: $(uname -m) -> $HOST_ARCH"

DOCKER_REGISTRY="${DOCKER_REGISTRY:-dacstest.azurecr.io}"
ACR_IMAGE="${DOCKER_REGISTRY%/}/native-build:linux-${HOST_ARCH}"

# Try to use ACR image first, fallback to local build if not available
nativeBuildImage="$ACR_IMAGE"
echo "Attempting to use ACR image: $nativeBuildImage"

if ! docker image inspect "$nativeBuildImage" >/dev/null 2>&1; then
    echo "ACR image not found locally. Attempting to pull from registry..."
    if docker pull "$nativeBuildImage" 2>/dev/null; then
        echo "Successfully pulled ACR image: $nativeBuildImage"
    else
        echo "Failed to pull from ACR. Building image locally..."
        nativeBuildImage="native-build:latest"
        native_build_args=()
        pip_build_secret=()
        if [ -n "$UBUNTU_ARCHIVE_MIRROR" ]; then
            native_build_args+=(--build-arg "UBUNTU_ARCHIVE_MIRROR=$UBUNTU_ARCHIVE_MIRROR")
        fi
        if [ "$USE_AZURE_CFS" = "true" ] && [ -n "${PIP_INDEX_URL:-}" ]; then
            pip_build_secret=(--secret "id=pip_index_url,env=PIP_INDEX_URL")
        fi
        docker build \
            "${native_build_args[@]}" \
            "${pip_build_secret[@]}" \
            -f src/images/native-build/Dockerfile \
            -t "$nativeBuildImage" \
            src/images/native-build
    fi
else
    echo "Using cached ACR image: $nativeBuildImage"
fi

# If arguments provided, run them; otherwise start interactive shell
if [ $# -eq 0 ]; then
    CMD="/bin/bash"
else
    CMD="$@"
fi

echo Running Native Build Container $nativeBuildImage

# Debug: Print environment variables being passed to container
echo "=== Environment variables being passed to container ==="
echo "BUILD_ARCH: ${BUILD_ARCH:-not set}"
echo "STORAGE_INTCPT_SAS_TOKEN: ${STORAGE_INTCPT_SAS_TOKEN:+[SET]}"
echo "SI_azBlobAccessToken: ${SI_azBlobAccessToken:+[SET]}"
echo "AZURE_CLIENT_ID: ${AZURE_CLIENT_ID:-not set}"
echo "USER_VM_AZURE_CLIENT_ID: ${USER_VM_AZURE_CLIENT_ID:-not set}"
echo "AZURE_TENANT_ID: ${AZURE_TENANT_ID:-not set}"
echo "AZURE_FEDERATED_TOKEN: ${AZURE_FEDERATED_TOKEN:+[SET]}"
echo "========================================================"

# Auto-detect InfiniBand devices for RDMA support
IB_DEVICES=""
IB_EXTRA_FLAGS=""
if [ -d /dev/infiniband ]; then
    echo "InfiniBand devices detected, enabling RDMA support"
    for dev in /dev/infiniband/*; do
        IB_DEVICES="${IB_DEVICES} --device ${dev}"
    done
    IB_EXTRA_FLAGS="--network=host --cap-add=IPC_LOCK --ulimit memlock=-1:-1 -v /sys/class/infiniband:/sys/class/infiniband:ro"
fi

# Keep large test scratch files off the container writable layer when TMPDIR
# points to a host directory with sufficient capacity.
TMPDIR_DOCKER_FLAGS=()
if [ -n "${TMPDIR:-}" ] && [ -d "${TMPDIR}" ]; then
    TMPDIR_DOCKER_FLAGS=(--env TMPDIR -v "${TMPDIR}:${TMPDIR}")
    echo "Mounting host TMPDIR into build container: ${TMPDIR}"
fi

docker run \
    --rm \
    -i \
    --ipc host \
    $TTY_OPTION \
    --privileged \
    --env AZURE_DEVOPS_EXT_PAT \
    --env STORAGE_INTCPT_SAS_TOKEN \
    --env SI_azBlobAccessToken \
    --env SI_remoteBlobUseSdkClient \
    --env SI_remoteBlobChecksumEnabled \
    --env AZURE_CLIENT_ID \
    --env USER_VM_AZURE_CLIENT_ID \
    --env AZURE_TENANT_ID \
    --env AZURE_FEDERATED_TOKEN \
    --env AZURE_BLOB_PATH \
    --env BUILD_ARCH \
    --env DACS_AZURE_STORAGE_ACCOUNT \
    --env DACS_AZURE_STORAGE_CONTAINER \
    --env DACS_AZURE_STORAGE_SECONDARY_CONTAINER \
    --env DACS_TEST_MODE \
    --env DACS_LIB_PATH \
    --env DACS_NATIVE_DIR \
    --env DACS_TEST_BLOB_PATH \
    --env DACS_WHEEL \
    --env PYTHON_PACKAGE_VERSION \
    --env PACKAGE_CHANNEL \
    --env BUILD_ARCHITECTURE \
    --env BUILD_CONFIGURATION \
    "${pip_runtime_args[@]}" \
    "${cargo_runtime_args[@]}" \
    -v /etc/group:/etc/group:ro \
    -v /etc/passwd:/etc/passwd:ro \
    -v /etc/shadow:/etc/shadow:ro \
    -v ~:/root \
    -v "$CARGO_CACHE_DIR/registry:/opt/cargo/registry" \
    -v "$CARGO_CACHE_DIR/git:/opt/cargo/git" \
    -v $DIR:/workspaces/$WORKSPACE_NAME \
    -v /var/run/docker.sock:/var/run/docker.sock \
    -w /workspaces/$WORKSPACE_NAME \
    "${TMPDIR_DOCKER_FLAGS[@]}" \
    $IB_DEVICES \
    $IB_EXTRA_FLAGS \
    $nativeBuildImage $CMD
