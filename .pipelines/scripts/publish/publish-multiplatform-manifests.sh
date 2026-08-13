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
IMAGES=("cache-server" "cache-test-client" "cacheserver-scheduler" "cache-controller")
BASE_IMAGES=("cache-server-base" "cache-client-base")

echo "Creating multi-platform manifests..."
echo "Registry: $REGISTRY"
echo "Build Number: $BUILD_NUMBER"
echo "Architectures: $BUILD_ARCHS"
echo ""

# Create multi-platform manifest from arch-specific images
create_manifest() {
    local image="$1"
    local tag="$2"
    local arch_pattern="$3"  # Use {arch} as placeholder
    
    local manifest_tag="$REGISTRY/$image:$tag"
    local arch_images=()
    
    echo "Creating manifest: $manifest_tag"
    echo "  References:"
    for arch in $BUILD_ARCHS; do
        local arch_image="$REGISTRY/$image:${arch_pattern//\{arch\}/$arch}"
        arch_images+=("$arch_image")
        echo "    • $arch_image"
    done
    
    docker buildx imagetools create --tag "$manifest_tag" "${arch_images[@]}"
    echo "Manifest created: $manifest_tag"
    echo ""
}

# Base images: combine <build>-<arch> into :latest (only if they were pushed in this build)
for image in "${BASE_IMAGES[@]}"; do
    # Check if amd64 tag exists to determine if base images were pushed in this build
    if docker buildx imagetools inspect "$REGISTRY/$image:${BUILD_NUMBER}-amd64" >/dev/null 2>&1; then
        echo "Base image $image was pushed in this build, creating manifest..."
        create_manifest "$image" "latest" "${BUILD_NUMBER}-{arch}"
    else
        echo "Skipping base image manifest for $image (not pushed in this build)"
    fi
done

# App images: combine <build>-linux-<arch> into :<build>
for image in "${IMAGES[@]}"; do
    create_manifest "$image" "$BUILD_NUMBER" "${BUILD_NUMBER}-linux-{arch}"
done

echo "✅ All multi-platform manifests created successfully:"
echo ""
for image in "${IMAGES[@]}"; do
    echo "  • $REGISTRY/$image:$BUILD_NUMBER (linux/${BUILD_ARCHS// /, linux/})"
done
echo ""
