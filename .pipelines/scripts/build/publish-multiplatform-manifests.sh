#!/bin/bash
# Create multi-platform manifests after all architecture builds complete
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# Load configuration
CONFIG_FILE="$REPOSITORY_ROOT/scripts/k8s/config/local.config"
if [[ -f "$CONFIG_FILE" ]]; then
    source "$CONFIG_FILE"
    echo "Loaded config from: $CONFIG_FILE"
fi

# Registry settings - can be overridden by environment variables
REGISTRY="${DOCKER_REGISTRY:-dacs.azurecr.io}"
BUILD_NUMBER="${BUILD_NUMBER:-latest}"

# Define images to process
IMAGES=("cache-server" "cache-test-client" "cacheserver-scheduler")

echo "Creating multi-platform manifests..."
echo "Registry: $REGISTRY"
echo "Build Number: $BUILD_NUMBER"
echo ""

# Create and push multi-platform manifests
for image in "${IMAGES[@]}"; do
    manifest_tag="$REGISTRY/$image:$BUILD_NUMBER"
    amd64_image="$REGISTRY/$image:$BUILD_NUMBER-linux-amd64"
    arm64_image="$REGISTRY/$image:$BUILD_NUMBER-linux-arm64"
    
    echo "Creating manifest: $manifest_tag"
    echo "  References:"
    echo "    • $amd64_image"
    echo "    • $arm64_image"
    
    docker buildx imagetools create --tag "$manifest_tag" "$amd64_image" "$arm64_image"
    
    echo "✓ Manifest created: $manifest_tag"
    echo ""
done

echo "✅ All multi-platform manifests created successfully:"
echo ""
for image in "${IMAGES[@]}"; do
    echo "  • $REGISTRY/$image:$BUILD_NUMBER (linux/amd64, linux/arm64)"
done
echo ""
