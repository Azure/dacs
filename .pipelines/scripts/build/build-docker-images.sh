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

# Build Docker images from Dockerfiles
cd "$DOCKER_DIR"
docker build -f CacheServer.Dockerfile -t "$CACHESERVER_IMAGE" .
docker build -f CacheClient.Dockerfile -t "$CACHECLIENT_IMAGE" . --build-context repo="$REPOSITORY_ROOT"
docker build -f CacheServerScheduler.Dockerfile -t "$CACHESERVER_SCHEDULER_IMAGE" "$REPOSITORY_ROOT/src/subsystems/storage/cache_server_scheduler"

cd "$CACHE_CONTROLLER_DIR"
IMG="$CACHECONTROLLER_IMAGE" make docker-build

echo ""
echo "Images built successfully"
docker images | grep -E "(cache*)" | grep local
