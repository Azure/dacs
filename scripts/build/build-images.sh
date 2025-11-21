#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DOCKER_DIR="$REPOSITORY_ROOT/Docker"

# Load configuration
CONFIG_FILE="$REPOSITORY_ROOT/scripts/k8s/config/local.config"
if [[ -f "$CONFIG_FILE" ]]; then
    source "$CONFIG_FILE"
    echo "Loaded config from: $CONFIG_FILE"
fi

# Image names (from config or defaults)
CACHESERVER_IMAGE="${CACHE_SERVER_IMAGE:-cacheserver:local}"
CACHECLIENT_IMAGE="${CACHE_CLIENT_IMAGE:-cacheclient:local}"

echo "Building Docker images..."
echo "Docker directory: $DOCKER_DIR"
echo "Cache Server Image: $CACHESERVER_IMAGE"
echo "Cache Client Image: $CACHECLIENT_IMAGE"

cd "$DOCKER_DIR"

docker build -f CacheServer.Dockerfile -t "$CACHESERVER_IMAGE" .
docker build -f CacheClient.Dockerfile -t "$CACHECLIENT_IMAGE" .

echo ""
echo "Images built successfully"
docker images | grep -E "(cacheserver|cacheclient)" | grep local