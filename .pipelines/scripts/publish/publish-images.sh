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

# Publish architecture-specific application and rebuilt base images.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

CONFIG_FILE="$REPOSITORY_ROOT/scripts/k8s/config/local.config"
if [[ -f "$CONFIG_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$CONFIG_FILE"
fi

REGISTRY="${DOCKER_REGISTRY:?DOCKER_REGISTRY is required}"
BUILD_NUMBER="${BUILD_NUMBER:?BUILD_NUMBER is required}"
BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
PUBLISH_LATEST="${PUBLISH_LATEST:-false}"
CURRENT_SHA="$(git rev-parse HEAD)"

IMAGES=("cache-server" "cache-test-client" "cacheserver-scheduler" "cache-controller")
BASE_IMAGES=("cache-server-base" "cache-client-base")

for image in "${IMAGES[@]}"; do
    if ! docker image inspect "${image}:local" >/dev/null 2>&1; then
        echo "Local image ${image}:local was not built" >&2
        exit 1
    fi
done

# A base image with the current SHA was rebuilt by this run. Unchanged PR
# builds retain the older label from the official image and do not republish it.
for image in "${BASE_IMAGES[@]}"; do
    source_image="${REGISTRY}/${image}:latest"
    image_sha="$(docker inspect "$source_image" \
        --format '{{ index .Config.Labels "git-sha"}}' 2>/dev/null || true)"

    if [[ "$image_sha" != "$CURRENT_SHA" ]]; then
        echo "Skipping unchanged base image: $image"
        continue
    fi

    immutable_image="${REGISTRY}/${image}:${BUILD_NUMBER}-${BUILD_ARCH}"
    docker tag "$source_image" "$immutable_image"
    docker push "$immutable_image"

    if [[ "$PUBLISH_LATEST" == "true" ]]; then
        rolling_image="${REGISTRY}/${image}:latest-linux-${BUILD_ARCH}"
        docker tag "$source_image" "$rolling_image"
        docker push "$rolling_image"
    fi
done

declare -A TARGET_IMAGES
for image in "${IMAGES[@]}"; do
    local_image="${image}:local"
    target_image="${REGISTRY}/${image}:${BUILD_NUMBER}-linux-${BUILD_ARCH}"
    TARGET_IMAGES["$image"]="$target_image"
    docker tag "$local_image" "$target_image"
    docker push "$target_image"
done

for image in "${IMAGES[@]}"; do
    docker rmi "${image}:local" >/dev/null 2>&1 || true
    docker rmi "${TARGET_IMAGES[$image]}" >/dev/null 2>&1 || true
done

cat > "$REPOSITORY_ROOT/docker-images.txt" <<EOF
CACHE_SERVER_IMAGE=${TARGET_IMAGES["cache-server"]}
CACHE_CLIENT_IMAGE=${TARGET_IMAGES["cache-test-client"]}
CACHE_SERVER_SCHEDULER_IMAGE=${TARGET_IMAGES["cacheserver-scheduler"]}
CACHE_CONTROLLER_IMAGE=${TARGET_IMAGES["cache-controller"]}
EOF

echo "Published immutable linux/${BUILD_ARCH} application images for ${BUILD_NUMBER}"
