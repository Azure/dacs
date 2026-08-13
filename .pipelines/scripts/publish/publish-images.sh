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

# Registry settings - must be provided by environment variables
REGISTRY="$DOCKER_REGISTRY"
BUILD_NUMBER="${BUILD_NUMBER:-latest}"
BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"

# Define images to process
IMAGES=("cache-server" "cache-test-client" "cacheserver-scheduler" "cache-controller")
BASE_IMAGES=("cache-server-base" "cache-client-base")

echo "Publishing Docker images..."
echo "Registry: $REGISTRY"
echo "Build Number: $BUILD_NUMBER"
echo "Architecture: $BUILD_ARCH"
echo ""

# Login to ACR if the registry is an Azure Container Registry
if [[ "$REGISTRY" == *".azurecr.io" ]]; then
    ACR_NAME="${REGISTRY%.azurecr.io}"
    echo "Logging into ACR $ACR_NAME..."
    if ! az acr login --name "$ACR_NAME" 2>&1; then
        echo "Error: Failed to login to ACR $ACR_NAME"
        exit 1
    fi
fi

# Validate that local images exist before publishing
echo "Validating local images..."
for image in "${IMAGES[@]}"; do
    local_image="${image}:local"
    if ! docker image inspect "$local_image" >/dev/null 2>&1; then
        echo "Error: Local image '$local_image' not found"
        echo "Please run .pipelines/scripts/build/build-docker-images.sh first"
        exit 1
    fi
done
echo "✓ All local images validated"
echo ""

# Check if base Dockerfiles changed
base_dockerfile_changed() {
    # Strategy 0: scheduled (cron) build -- always push so security patches
    # for transitive system packages (libc6, libssl3, etc.), picked up by
    # `apt-get upgrade` during the scheduled rebuild, actually land in ACR.
    # Mirrors the same logic in build-docker-images.sh.
    if [[ "${BUILD_REASON:-}" == "Schedule" ]]; then
        echo "[DEBUG] Forcing base image push (BUILD_REASON=Schedule)" >&2
        return 0
    fi

    # Strategy 1: PR builds - compare against origin/main
    git fetch origin main --depth=1 2>/dev/null || true
    if git diff --name-only origin/main...HEAD 2>/dev/null | grep -q "Docker/base/"; then
        echo "[DEBUG] Docker/base/ changed (vs origin/main)" >&2
        return 0
    fi

    # Compare against commit SHA
    # Checking one base image should be sufficient just for sake of completeness comparing all base images
    for docker_image_name in "${BASE_IMAGES[@]}"; do

        # Strategy 2: Official builds - compare against commit SHA stored in ACR image label
        local image="${REGISTRY}/${docker_image_name}:latest"
        local last_built_sha

        # Pull the image to inspect commit SHA label; if pull fails, assume image doesn't exist and needs build
        docker pull "$image" > /dev/null 2>&1 || {
            echo "[DEBUG] Failed to pull $image, assuming base image needs to be pushed" >&2
            return 0
        }

        last_built_sha=$(docker inspect "$image" --format '{{ index .Config.Labels "git-sha"}}' 2>/dev/null || true)
        if [[ -z "$last_built_sha" || "$last_built_sha" == "<no value>" ]]; then
            echo "[DEBUG] No valid git-sha label found on $image, pushing to be safe" >&2
            return 0
        fi

        echo "[DEBUG] Last base image built at commit: $last_built_sha" >&2

        # Ensure the SHA exists locally - deepen the fetch if needed
        git fetch --unshallow 2>/dev/null || git fetch origin --deepen=100 2>/dev/null || true

        if ! git rev-parse --quiet --verify "$last_built_sha" >/dev/null 2>&1; then
            echo "[DEBUG] Commit $last_built_sha still not found, pushing to be safe" >&2
            return 0
        fi

        if git diff --name-only "$last_built_sha"...HEAD 2>/dev/null | grep -q "Docker/base/"; then
            echo "[DEBUG] Docker/base/ changed since $last_built_sha" >&2
            return 0
        fi
    done

    echo "[DEBUG] No Docker/base/ changes detected" >&2
    return 1
}

# Push base images only if Dockerfile changed
if base_dockerfile_changed; then
    echo "Pushing base images (Dockerfile changed)..."
    for image in "${BASE_IMAGES[@]}"; do
        src="${REGISTRY}/${image}:latest"
        dst="${REGISTRY}/${image}:${BUILD_NUMBER}-${BUILD_ARCH}"
        if docker image inspect "$src" >/dev/null 2>&1; then
            echo "  Tagging $src → $dst"
            docker tag "$src" "$dst"
            echo "  Pushing $dst"
            docker push "$dst"
        fi
    done
    echo ""
else
    echo "Skipping base image push (no Dockerfile changes)"
    echo ""
fi

# Tag and push images
echo "Tagging and pushing platform-specific images..."
declare -A TARGET_IMAGES

for image in "${IMAGES[@]}"; do
    local_image="${image}:local"
    target_image="$REGISTRY/$image:$BUILD_NUMBER-linux-$BUILD_ARCH"
    
    TARGET_IMAGES["$image"]="$target_image"
    
    echo "  Tagging $local_image → $target_image"
    docker tag "$local_image" "$target_image"
    
    echo "  Pushing $target_image"
    docker push "$target_image"
    echo ""
done

echo "✓ All platform-specific images pushed"
echo ""

# Remove local images to free disk space
echo "Cleaning up local Docker images..."
for image in "${IMAGES[@]}"; do
    local_image="${image}:local"
    docker rmi "$local_image" 2>/dev/null || echo "  Failed to remove $local_image"
    docker rmi "${TARGET_IMAGES[$image]}" 2>/dev/null || echo "  Failed to remove ${TARGET_IMAGES[$image]}"
done
echo "✓ Local Docker cleanup completed"
echo ""

# Generate artifact file with published image references
echo "Creating image coordinates artifact..."
cat > "$REPOSITORY_ROOT/docker-images.txt" << EOF
CACHE_SERVER_IMAGE=${TARGET_IMAGES["cache-server"]}
CACHE_CLIENT_IMAGE=${TARGET_IMAGES["cache-test-client"]}
CACHE_SERVER_SCHEDULER_IMAGE=${TARGET_IMAGES["cacheserver-scheduler"]}
CACHE_CONTROLLER_IMAGE=${TARGET_IMAGES["cache-controller"]}
EOF

echo ""
echo "✅ Platform-specific images published successfully:"
echo ""
for image in "${IMAGES[@]}"; do
    echo "  • ${TARGET_IMAGES[$image]}"
done
echo ""
echo "Image coordinates saved to docker-images.txt"
echo ""
echo "Note: Multi-platform manifests will be created in a separate step after all architectures complete."