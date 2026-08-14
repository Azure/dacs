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

# Load configuration from k8s config file
CONFIG_FILE="$REPOSITORY_ROOT/scripts/k8s/config/local.config"
if [[ -f "$CONFIG_FILE" ]]; then
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
    REGISTRY="dacs.azurecr.io"
    echo "DOCKER_REGISTRY not set; defaulting to: $REGISTRY"
fi
echo "image registry: $REGISTRY"

# Check if base Dockerfiles changed
base_dockerfile_changed() {
    docker_image_name="$1"

    # Strategy 0: scheduled (cron) build -- always rebuild so security patches
    # for transitive system packages (libc6, libssl3, etc.) are picked up even
    # when Docker/base/ files have not changed.
    if [[ "${BUILD_REASON:-}" == "Schedule" ]]; then
        echo "[DEBUG] Forcing base image rebuild (BUILD_REASON=Schedule)" >&2
        return 0
    fi

    # Strategy 1: PR builds - compare against origin/main
    git fetch origin main --depth=1 2>/dev/null || true
    if git diff --name-only origin/main...HEAD 2>/dev/null | grep -q "Docker/base/"; then
        echo "[DEBUG] Docker/base/ changed (vs origin/main)" >&2
        return 0
    fi

    # Strategy 2: Official builds - compare against commit SHA stored in ACR image label
    local image="${REGISTRY}/${docker_image_name}:latest"
    local last_built_sha

    # Pull the image to inspect commit SHA label; if pull fails, assume image doesn't exist and needs build
    docker pull "$image" > /dev/null 2>&1 || {
        echo "[DEBUG] Failed to pull $image, assuming base image needs rebuild" >&2
        return 0
    }

    last_built_sha=$(docker inspect "$image" --format '{{ index .Config.Labels "git-sha"}}' 2>/dev/null || true)
    if [[ -z "$last_built_sha" || "$last_built_sha" == "<no value>" ]]; then
        echo "[DEBUG] No git-sha label found on $image, rebuilding to be safe" >&2
        return 0
    fi

    echo "[DEBUG] Last base image built at commit: $last_built_sha" >&2

    # Ensure the SHA exists locally -- if not, deepen the fetch to find it, but if it still doesn't exist, assume we need to rebuild
    git fetch --unshallow 2>/dev/null || git fetch origin --deepen=100 2>/dev/null || true

    if ! git rev-parse --quiet --verify "$last_built_sha" >/dev/null 2>&1; then
        echo "[DEBUG] Last built SHA $last_built_sha still not found, assuming base image needs rebuild" >&2
        return 0
    fi

    if git diff --name-only "$last_built_sha"...HEAD 2>/dev/null | grep -q "Docker/base/"; then
        echo "[DEBUG] Docker/base/ changed since $last_built_sha" >&2
        return 0
    fi

    echo "[DEBUG] No Docker/base/ changes detected" >&2
    return 1
}

prepare_base_image() {
    local name="$1"
    local dockerfile="$2"
    local image="${REGISTRY}/${name}:latest"
    local current_sha
    current_sha=$(git rev-parse HEAD)

    # On scheduled rebuilds, bypass Docker's layer cache so that
    # `apt-get update && apt-get upgrade` actually re-runs and pulls fresh
    # security patches. Without this, warm agents would reuse a cached layer
    # whose RUN string is byte-identical to last week and skip the upgrade.
    # `--pull` additionally forces re-resolving the FROM tag against the
    # registry so the upstream ubuntu:22.04 layer itself is refreshed.
    local no_cache_flag=""
    if [[ "${BUILD_REASON:-}" == "Schedule" ]]; then
        no_cache_flag="--no-cache --pull"
    fi

    if base_dockerfile_changed "$name"; then
        echo "Building $name (Dockerfile changed)..."
        docker build $no_cache_flag \
            --build-context compliance="$REPOSITORY_ROOT" \
            --label "git-sha=$current_sha" \
            -f "$REPOSITORY_ROOT/$dockerfile" \
            -t "$image" \
            "$REPOSITORY_ROOT/Docker/base/"
    else
        # Since image is already pulled via base_dockerfile_changed, it will be a no-op if it exists.
        echo "Attempting to pull base image: $image"
        if docker pull "$image"; then
            echo "Pulled $name from ACR"
        else
            echo "Pull failed for $image, building locally..."
            docker build $no_cache_flag \
                --build-context compliance="$REPOSITORY_ROOT" \
                --label "git-sha=$current_sha" \
                -f "$REPOSITORY_ROOT/$dockerfile" \
                -t "$image" \
                "$REPOSITORY_ROOT/Docker/base/"
        fi
    fi
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
    --build-context compliance="$REPOSITORY_ROOT" \
    -f CacheServerScheduler.Dockerfile \
    -t "$CACHESERVER_SCHEDULER_IMAGE" \
    "$REPOSITORY_ROOT/src/subsystems/storage/cache_server_scheduler" &
PID_SCHEDULER=$!

cd "$CACHE_CONTROLLER_DIR"
COMPLIANCE_CONTEXT="$REPOSITORY_ROOT" IMG="$CACHECONTROLLER_IMAGE" make docker-build &
PID_CONTROLLER=$!

wait $PID_SERVER || { echo "CacheServer build failed"; exit 1; }
wait $PID_CLIENT || { echo "CacheClient build failed"; exit 1; }
wait $PID_SCHEDULER || { echo "CacheServerScheduler build failed"; exit 1; }
wait $PID_CONTROLLER || { echo "CacheController build failed"; exit 1; }

echo ""
echo "Images built successfully"
docker images | grep -E "(cache*)" | grep local
