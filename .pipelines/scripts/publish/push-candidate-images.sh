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

# Push locally built images under build-specific candidate tags.
set -euo pipefail

REGISTRY="${DOCKER_REGISTRY:?DOCKER_REGISTRY is required}"
BUILD_ID="${BUILD_ID:?BUILD_ID is required}"
BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"

if [[ ! "$BUILD_ID" =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]*$ ]]; then
    echo "Invalid BUILD_ID: $BUILD_ID" >&2
    exit 1
fi
case "$BUILD_ARCH" in
    amd64|arm64) ;;
    *)
        echo "Unsupported BUILD_ARCH: $BUILD_ARCH" >&2
        exit 1
        ;;
esac

IMAGES=("cache-server" "cache-test-client" "cacheserver-scheduler" "cache-controller" "cache-setup")
PIDS=()

for image in "${IMAGES[@]}"; do
    source_image="${image}:local"
    candidate_image="${REGISTRY}/${image}:candidate-${BUILD_ID}-linux-${BUILD_ARCH}"
    docker image inspect "$source_image" >/dev/null
    docker tag "$source_image" "$candidate_image"
    docker push "$candidate_image" &
    PIDS+=("$!")
done

status=0
for index in "${!PIDS[@]}"; do
    if ! wait "${PIDS[$index]}"; then
        echo "Failed to push candidate image: ${IMAGES[$index]}" >&2
        status=1
    fi
done
exit "$status"
