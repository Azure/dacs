#!/bin/bash
# Build Docker images for cache server and client components
set -e

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
REGISTRY="${DOCKER_REGISTRY}"
if [[ -z "$REGISTRY" ]]; then
    REGISTRY="dacs.acr.io"
    echo "DOCKER_REGISTRY not set; defaulting to: $REGISTRY"
fi
echo "image registry: $REGISTRY"

# Check if base Dockerfiles changed
base_dockerfile_changed() {
    # Try to fetch origin/main for comparison
    git fetch origin main --depth=1 2>/dev/null || true
    
    # Compare against origin/main (for PR builds)
    if git diff --name-only origin/main...HEAD 2>/dev/null | grep -q "Docker/base/"; then
        echo "[DEBUG] Docker/base/ changed (vs origin/main)" >&2
        return 0
    fi
    
    # Compare against previous commit (for official builds on main)
    if git diff --name-only HEAD~1 2>/dev/null | grep -q "Docker/base/"; then
        echo "[DEBUG] Docker/base/ changed (vs HEAD~1)" >&2
        return 0
    fi
    
    echo "[DEBUG] No Docker/base/ changes detected" >&2
    return 1
}

prepare_base_image() {
    local name="$1"
    local dockerfile="$2"
    local image="${REGISTRY}/${name}:latest"
    
    if base_dockerfile_changed; then
        echo "Building $name (Dockerfile changed)..."
        docker build -f "$REPOSITORY_ROOT/$dockerfile" -t "$image" "$REPOSITORY_ROOT/Docker/base/"
    else
        echo "Attempting to pull base image: $image"
        if docker pull "$image"; then
            echo "Pulled $name from ACR"
        else
            echo "Pull failed for $image, building locally..."
            docker build -f "$REPOSITORY_ROOT/$dockerfile" -t "$image" "$REPOSITORY_ROOT/Docker/base/"
        fi
    fi
}

prepare_base_image "cache-server-base" "Docker/base/CacheServer.base.Dockerfile"
prepare_base_image "cache-client-base" "Docker/base/CacheClient.base.Dockerfile"

# Build Docker images from Dockerfiles
cd "$DOCKER_DIR"
docker build --build-arg DOCKER_REGISTRY="$REGISTRY" -f CacheServer.Dockerfile -t "$CACHESERVER_IMAGE" .
docker build --build-arg DOCKER_REGISTRY="$REGISTRY" -f CacheClient.Dockerfile -t "$CACHECLIENT_IMAGE" . --build-context repo="$REPOSITORY_ROOT"
docker build -f CacheServerScheduler.Dockerfile -t "$CACHESERVER_SCHEDULER_IMAGE" "$REPOSITORY_ROOT/src/subsystems/storage/cache_server_scheduler"

cd "$CACHE_CONTROLLER_DIR"
IMG="$CACHECONTROLLER_IMAGE" make docker-build

echo ""
echo "Images built successfully"
docker images | grep -E "(cache*)" | grep local
