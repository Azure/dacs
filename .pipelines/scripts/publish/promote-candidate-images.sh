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

# Promote build-specific candidates to immutable architecture and manifest tags.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REGISTRY="${DOCKER_REGISTRY:?DOCKER_REGISTRY is required}"
BUILD_ID="${BUILD_ID:?BUILD_ID is required}"
BUILD_NUMBER="${BUILD_NUMBER:?BUILD_NUMBER is required}"
BUILD_ARCHS="${BUILD_ARCHS:-amd64 arm64}"
IMAGES=("cache-server" "cache-test-client" "cacheserver-scheduler" "cache-controller")

read -r -a architectures <<<"$BUILD_ARCHS"
if [[ "${#architectures[@]}" -eq 0 ]]; then
    echo "BUILD_ARCHS must contain at least one architecture" >&2
    exit 1
fi
for arch in "${architectures[@]}"; do
    case "$arch" in
        amd64|arm64) ;;
        *)
            echo "Unsupported architecture in BUILD_ARCHS: $arch" >&2
            exit 1
            ;;
    esac
done

for arch in "${architectures[@]}"; do
    for image in "${IMAGES[@]}"; do
        candidate="${REGISTRY}/${image}:candidate-${BUILD_ID}-linux-${arch}"
        destination="${REGISTRY}/${image}:${BUILD_NUMBER}-linux-${arch}"
        docker buildx imagetools inspect "$candidate" >/dev/null
        docker buildx imagetools create --tag "$destination" "$candidate"
    done
done

DOCKER_REGISTRY="$REGISTRY" \
BUILD_NUMBER="$BUILD_NUMBER" \
BUILD_ARCHS="$BUILD_ARCHS" \
    "$SCRIPT_DIR/publish-multiplatform-manifests.sh"
