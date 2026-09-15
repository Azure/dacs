#!/bin/bash
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

# Build Docker images for cache server and client components
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
DOCKER_DIR="$REPOSITORY_ROOT/Docker"
CACHE_CONTROLLER_DIR="$REPOSITORY_ROOT/src/subsystems/cache-controller"
SCHEDULER_DIR="$REPOSITORY_ROOT/src/subsystems/storage/cache_server_scheduler"
BASE_IMAGE_DIR="$REPOSITORY_ROOT/Docker/base"
BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
ENABLE_REGISTRY_BUILD_CACHE="${ENABLE_REGISTRY_BUILD_CACHE:-false}"
BUILDKIT_IMAGE="${BUILDKIT_IMAGE:-mcr.microsoft.com/oss/v2/moby/buildkit:v0.31.0@sha256:fe54b035f7e18247c19d74adfaae42f62f44dca1fd00ae35b795a662a6126d67}"
UBUNTU_ARCHIVE_MIRROR="${UBUNTU_ARCHIVE_MIRROR:-}"
if [[ "${USE_AZURE_CFS:-false}" == "true" && -z "$UBUNTU_ARCHIVE_MIRROR" ]]; then
    UBUNTU_ARCHIVE_MIRROR="http://azure.archive.ubuntu.com/ubuntu"
fi

for context_dir in "$BASE_IMAGE_DIR" "$SCHEDULER_DIR" "$CACHE_CONTROLLER_DIR"; do
    cp "$REPOSITORY_ROOT/LICENSE" "$context_dir/LICENSE"
    cp "$REPOSITORY_ROOT/NOTICE.txt" "$context_dir/NOTICE.txt"
done

# Load configuration from k8s config file
CONFIG_FILE="$REPOSITORY_ROOT/scripts/k8s/config/local.config"
if [[ -f "$CONFIG_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$CONFIG_FILE"
    echo "Loaded config from: $CONFIG_FILE"
fi

# Image names (from config or defaults)
CACHESERVER_IMAGE="${CACHE_SERVER_IMAGE:-cache-server:local}"
CACHECLIENT_IMAGE="${CACHE_CLIENT_IMAGE:-cache-test-client:local}"
CACHESERVER_SCHEDULER_IMAGE="${CACHE_SERVER_SCHEDULER_IMAGE:-cacheserver-scheduler:local}"
CACHECONTROLLER_IMAGE="${CACHE_CONTROLLER_IMAGE:-cache-controller:local}"

echo "Building Docker images..."
echo "Docker directory: $DOCKER_DIR"
echo "Cache Server Image: $CACHESERVER_IMAGE"
echo "Cache Client Image: $CACHECLIENT_IMAGE"
echo "Cache Server Scheduler Image: $CACHESERVER_SCHEDULER_IMAGE"
echo "Cache Controller Image: $CACHECONTROLLER_IMAGE"

# Registry for base images
# Note: DOCKER_REGISTRY already includes .azurecr.io suffix from pipeline
REGISTRY="${DOCKER_REGISTRY:-}"
if [[ -z "$REGISTRY" ]]; then
    REGISTRY="dacstest.azurecr.io"
    echo "DOCKER_REGISTRY not set; defaulting to: $REGISTRY"
fi
echo "image registry: $REGISTRY"

# Reclaim disk before we start building images. Warm CI agents accumulate
# gigabytes of stale BuildKit layer cache from prior runs. That cache is a
# pure performance optimization; the (re)builds below will repopulate what
# they need. Without this, the cache-client-base rebuild -- which now
# contains a Go-based blobfuse2 builder stage -- can hit ENOSPC on agents
# that were already near their disk quota (~95%) before this job started.
echo "Pruning stale Docker BuildKit cache to reclaim disk..."
docker builder prune -af >/dev/null 2>&1 || true
docker image prune -f >/dev/null 2>&1 || true
df -h / | tail -1 || true

BUILDER_NAME=""
cleanup() {
    if [[ -n "$BUILDER_NAME" ]]; then
        docker buildx rm "$BUILDER_NAME" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

if [[ "$ENABLE_REGISTRY_BUILD_CACHE" == "true" ]]; then
    BUILDER_NAME="dacs-images-${BUILD_ARCH}-$$"
    docker buildx create \
        --name "$BUILDER_NAME" \
        --driver docker-container \
        --driver-opt "image=${BUILDKIT_IMAGE}" \
        --use >/dev/null
    docker buildx inspect --bootstrap >/dev/null
fi

build_base_image() {
    local name="$1"
    local dockerfile="$2"
    local image="$3"
    local current_sha="$4"
    shift 4
    local build_args=("$@")
    local build_command=(docker build)
    local build_options=(--pull)

    if [[ "$ENABLE_REGISTRY_BUILD_CACHE" == "true" ]]; then
        local cache_image="${REGISTRY}/${name}:buildcache-linux-${BUILD_ARCH}"
        build_command=(docker buildx build)
        build_options+=(
            --platform "linux/${BUILD_ARCH}"
            --cache-from "type=registry,ref=${cache_image}"
            --cache-to "type=registry,ref=${cache_image},mode=max"
            --load
        )
    fi

    "${build_command[@]}" "${build_options[@]}" \
        "${build_args[@]}" \
        --label "git-sha=$current_sha" \
        -f "$REPOSITORY_ROOT/$dockerfile" \
        -t "$image" \
        "$REPOSITORY_ROOT/Docker/base/"
}

prepare_base_image() {
    local name="$1"
    local dockerfile="$2"
    local image="${REGISTRY}/${name}:latest"
    local current_sha
    current_sha=$(git rev-parse HEAD)
    local build_args=()
    if [[ -n "$UBUNTU_ARCHIVE_MIRROR" ]]; then
        build_args+=(--build-arg "UBUNTU_ARCHIVE_MIRROR=$UBUNTU_ARCHIVE_MIRROR")
    fi
    if [[ "${USE_AZURE_CFS:-false}" == "true" && "$name" == "cache-client-base" && -n "${PIP_INDEX_URL:-}" ]]; then
        build_args+=(--secret "id=pip_index_url,env=PIP_INDEX_URL")
    fi

    echo "Building $name..."
    build_base_image "$name" "$dockerfile" "$image" "$current_sha" \
        "${build_args[@]}"
}

prepare_base_image "cache-server-base" "Docker/base/CacheServer.base.Dockerfile"
prepare_base_image "cache-client-base" "Docker/base/CacheClient.base.Dockerfile"

# Build Docker images from Dockerfiles
cd "$DOCKER_DIR"

# Build C++ images in parallel
docker build --build-arg DOCKER_REGISTRY="$REGISTRY" -f CacheServer.Dockerfile -t "$CACHESERVER_IMAGE" . &
PID_SERVER=$!
docker build --build-arg DOCKER_REGISTRY="$REGISTRY" -f CacheClient.Dockerfile -t "$CACHECLIENT_IMAGE" . &
PID_CLIENT=$!

# Build Go images in parallel
docker build \
    -f CacheServerScheduler.Dockerfile \
    -t "$CACHESERVER_SCHEDULER_IMAGE" \
    "$REPOSITORY_ROOT/src/subsystems/storage/cache_server_scheduler" &
PID_SCHEDULER=$!

cd "$CACHE_CONTROLLER_DIR"
IMG="$CACHECONTROLLER_IMAGE" make docker-build &
PID_CONTROLLER=$!

wait $PID_SERVER || { echo "CacheServer build failed"; exit 1; }
wait $PID_CLIENT || { echo "CacheClient build failed"; exit 1; }
wait $PID_SCHEDULER || { echo "CacheServerScheduler build failed"; exit 1; }
wait $PID_CONTROLLER || { echo "CacheController build failed"; exit 1; }

echo ""
echo "Images built successfully"
docker images | grep -E 'cache.*' | grep local
