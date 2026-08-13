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
mkdir -p "$CARGO_CACHE_DIR/registry" "$CARGO_CACHE_DIR/git"

# Detect host architecture and use architecture-specific image tag
HOST_ARCH=$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')
echo "Detected host architecture: $(uname -m) -> $HOST_ARCH"

# TODO: switch to dacs
ACR_IMAGE="dacs.azurecr.io/native-build:linux-${HOST_ARCH}"

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
        docker build -f src/images/native-build/Dockerfile -t "$nativeBuildImage" src/images/native-build
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

docker run \
    --rm \
    -i \
    --ipc host \
    $TTY_OPTION \
    --privileged \
    --env AZURE_DEVOPS_EXT_PAT \
    --env STORAGE_INTCPT_SAS_TOKEN \
    --env SI_azBlobAccessToken \
    --env AZURE_CLIENT_ID \
    --env USER_VM_AZURE_CLIENT_ID \
    --env AZURE_TENANT_ID \
    --env AZURE_FEDERATED_TOKEN \
    --env BUILD_ARCH \
    --env DACS_TEST_MODE \
    --env DACS_LIB_PATH \
    --env DACS_NATIVE_DIR \
    --env DACS_TEST_BLOB_PATH \
    --env DACS_WHEEL \
    --env PYTHON_PACKAGE_VERSION \
    --env PACKAGE_CHANNEL \
    --env BUILD_ARCHITECTURE \
    --env BUILD_CONFIGURATION \
    -v /etc/group:/etc/group:ro \
    -v /etc/passwd:/etc/passwd:ro \
    -v /etc/shadow:/etc/shadow:ro \
    -v ~:/root \
    -v "$CARGO_CACHE_DIR/registry:/opt/cargo/registry" \
    -v "$CARGO_CACHE_DIR/git:/opt/cargo/git" \
    -v $DIR:/workspaces/$WORKSPACE_NAME \
    -v /var/run/docker.sock:/var/run/docker.sock \
    -w /workspaces/$WORKSPACE_NAME \
    $IB_DEVICES \
    $IB_EXTRA_FLAGS \
    $nativeBuildImage $CMD
