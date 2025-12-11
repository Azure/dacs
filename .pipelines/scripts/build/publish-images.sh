#!/bin/bash
# Publish Docker images to Azure Container Registry and create artifacts
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# Load configuration
CONFIG_FILE="$REPOSITORY_ROOT/scripts/k8s/config/local.config"
if [[ -f "$CONFIG_FILE" ]]; then
    source "$CONFIG_FILE"
    echo "Loaded config from: $CONFIG_FILE"
fi

# Default image names (what build-docker-images.sh creates)
CACHESERVER_LOCAL_IMAGE="${CACHE_SERVER_IMAGE:-cache-server:local}"
CACHECLIENT_LOCAL_IMAGE="${CACHE_CLIENT_IMAGE:-cache-test-client:local}"
CACHESERVER_SCHEDULER_LOCAL_IMAGE="${CACHE_SERVER_SCHEDULER_IMAGE:-cacheserver-scheduler:local}"

# Registry settings - can be overridden by environment variables
REGISTRY="${DOCKER_REGISTRY:-dacs.azurecr.io}"
REGISTRY_NAMESPACE="${DOCKER_REGISTRY_NAMESPACE:-dacs/images}"
BUILD_NUMBER="${BUILD_NUMBER:-latest}"

# Target image names
CACHESERVER_TARGET_IMAGE="$REGISTRY/$REGISTRY_NAMESPACE/cache-server:$BUILD_NUMBER-linux-amd64"
CACHECLIENT_TARGET_IMAGE="$REGISTRY/$REGISTRY_NAMESPACE/cache-test-client:$BUILD_NUMBER-linux-amd64"
CACHESERVER_SCHEDULER_TARGET_IMAGE="$REGISTRY/$REGISTRY_NAMESPACE/cacheserver-scheduler:$BUILD_NUMBER-linux-amd64"
CACHESERVER_LATEST_IMAGE="$REGISTRY/$REGISTRY_NAMESPACE/cache-server:latest"
CACHECLIENT_LATEST_IMAGE="$REGISTRY/$REGISTRY_NAMESPACE/cache-test-client:latest"
CACHESERVER_SCHEDULER_LATEST_IMAGE="$REGISTRY/$REGISTRY_NAMESPACE/cacheserver-scheduler:latest"

echo "Publishing Docker images..."
echo "Registry: $REGISTRY"
echo "Build Number: $BUILD_NUMBER"
echo ""

# Validate that local images exist before publishing
if ! docker image inspect "$CACHESERVER_LOCAL_IMAGE" >/dev/null 2>&1; then
    echo "Error: Local cache server image '$CACHESERVER_LOCAL_IMAGE' not found"
    echo "Please run .pipelines/scripts/build/build-docker-images.sh first"
    exit 1
fi

if ! docker image inspect "$CACHECLIENT_LOCAL_IMAGE" >/dev/null 2>&1; then
    echo "Error: Local cache client image '$CACHECLIENT_LOCAL_IMAGE' not found"
    echo "Please run .pipelines/scripts/build/build-docker-images.sh first"
    exit 1
fi

if ! docker image inspect "$CACHESERVER_SCHEDULER_LOCAL_IMAGE" >/dev/null 2>&1; then
    echo "Error: Local cache server scheduler image '$CACHESERVER_SCHEDULER_LOCAL_IMAGE' not found"
    echo "Please run .pipelines/scripts/build/build-docker-images.sh first"
    exit 1
fi

# Tag local images with registry names and build numbers
echo "Tagging images for registry..."
docker tag "$CACHESERVER_LOCAL_IMAGE" "$CACHESERVER_TARGET_IMAGE"
docker tag "$CACHECLIENT_LOCAL_IMAGE" "$CACHECLIENT_TARGET_IMAGE"
docker tag "$CACHESERVER_SCHEDULER_LOCAL_IMAGE" "$CACHESERVER_SCHEDULER_TARGET_IMAGE"
docker tag "$CACHESERVER_LOCAL_IMAGE" "$CACHESERVER_LATEST_IMAGE"
docker tag "$CACHECLIENT_LOCAL_IMAGE" "$CACHECLIENT_LATEST_IMAGE"
docker tag "$CACHESERVER_SCHEDULER_LOCAL_IMAGE" "$CACHESERVER_SCHEDULER_LATEST_IMAGE"

# Push tagged images to Azure Container Registry
echo "Pushing images to registry..."
docker push "$CACHESERVER_TARGET_IMAGE"
docker push "$CACHECLIENT_TARGET_IMAGE"
docker push "$CACHESERVER_SCHEDULER_TARGET_IMAGE"
docker push "$CACHESERVER_LATEST_IMAGE"
docker push "$CACHECLIENT_LATEST_IMAGE"
docker push "$CACHESERVER_SCHEDULER_LATEST_IMAGE"

# Remove local images to free disk space
echo "Cleaning up local Docker images..."
docker rmi "$CACHESERVER_LOCAL_IMAGE" || echo "Failed to remove $CACHESERVER_LOCAL_IMAGE"
docker rmi "$CACHECLIENT_LOCAL_IMAGE" || echo "Failed to remove $CACHECLIENT_LOCAL_IMAGE"
docker rmi "$CACHESERVER_SCHEDULER_LOCAL_IMAGE" || echo "Failed to remove $CACHESERVER_SCHEDULER_LOCAL_IMAGE"
docker rmi "$CACHESERVER_TARGET_IMAGE" || echo "Failed to remove $CACHESERVER_TARGET_IMAGE"
docker rmi "$CACHECLIENT_TARGET_IMAGE" || echo "Failed to remove $CACHECLIENT_TARGET_IMAGE"
docker rmi "$CACHESERVER_SCHEDULER_TARGET_IMAGE" || echo "Failed to remove $CACHESERVER_SCHEDULER_TARGET_IMAGE"
docker rmi "$CACHESERVER_LATEST_IMAGE" || echo "Failed to remove $CACHESERVER_LATEST_IMAGE"
docker rmi "$CACHECLIENT_LATEST_IMAGE" || echo "Failed to remove $CACHECLIENT_LATEST_IMAGE"
docker rmi "$CACHESERVER_SCHEDULER_LATEST_IMAGE" || echo "Failed to remove $CACHESERVER_SCHEDULER_LATEST_IMAGE"

echo "Local Docker cleanup completed"

# Generate artifact file with published image references
echo "Creating image coordinates artifact..."
cat > "$REPOSITORY_ROOT/docker-images.txt" << EOF
CACHE_SERVER_IMAGE=$CACHESERVER_TARGET_IMAGE
CACHE_CLIENT_IMAGE=$CACHECLIENT_TARGET_IMAGE
CACHE_SERVER_SCHEDULER_IMAGE=$CACHESERVER_SCHEDULER_TARGET_IMAGE
EOF

echo ""
echo "Images published successfully:"
echo "  Cache Server: $CACHESERVER_TARGET_IMAGE"
echo "  Cache Client: $CACHECLIENT_TARGET_IMAGE"
echo "  Cache Server Scheduler: $CACHESERVER_SCHEDULER_TARGET_IMAGE"
echo "  Also tagged as: latest"
echo ""
echo "Image coordinates saved to docker-images.txt"