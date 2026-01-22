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

# Registry settings - must be provided by environment variables
REGISTRY="$DOCKER_REGISTRY"
BUILD_NUMBER="${BUILD_NUMBER:-latest}"

# Architectures to include in manifest
# Default: both amd64 and arm64 (for pipeline)
# Override with BUILD_ARCHS="amd64" for single-arch local builds
BUILD_ARCHS="${BUILD_ARCHS:-amd64 arm64}"

# Define images to process
IMAGES=("cache-server" "cache-test-client" "cacheserver-scheduler")

echo "Creating multi-platform manifests..."
echo "Registry: $REGISTRY"
echo "Build Number: $BUILD_NUMBER"
echo "Architectures: $BUILD_ARCHS"
echo ""

# Create and push multi-platform manifests
for image in "${IMAGES[@]}"; do
    manifest_tag="$REGISTRY/$image:$BUILD_NUMBER"
    
    # Build list of arch-specific images
    arch_images=()
    echo "Creating manifest: $manifest_tag"
    echo "  References:"
    for arch in $BUILD_ARCHS; do
        arch_image="$REGISTRY/$image:$BUILD_NUMBER-linux-$arch"
        arch_images+=("$arch_image")
        echo "    • $arch_image"
    done
    
    docker buildx imagetools create --tag "$manifest_tag" "${arch_images[@]}"
    
    echo "✓ Manifest created: $manifest_tag"
    echo ""
done

echo "✅ All multi-platform manifests created successfully:"
echo ""
for image in "${IMAGES[@]}"; do
    echo "  • $REGISTRY/$image:$BUILD_NUMBER (linux/${BUILD_ARCHS// /, linux/})"
done
echo ""
