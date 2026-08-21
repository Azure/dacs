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

# Create immutable manifests for every build and mutable aliases for official
# main builds.
set -euo pipefail

REGISTRY="${DOCKER_REGISTRY:?DOCKER_REGISTRY is required}"
BUILD_NUMBER="${BUILD_NUMBER:?BUILD_NUMBER is required}"
BUILD_ARCHS="${BUILD_ARCHS:-amd64 arm64}"
PUBLISH_LATEST="${PUBLISH_LATEST:-false}"

IMAGES=("cache-server" "cache-test-client" "cacheserver-scheduler" "cache-controller")
BASE_IMAGES=("cache-server-base" "cache-client-base")

create_manifest() {
    local image="$1"
    local tag="$2"
    local arch_pattern="$3"
    local arch_images=()

    for arch in $BUILD_ARCHS; do
        arch_images+=("${REGISTRY}/${image}:${arch_pattern//\{arch\}/$arch}")
    done

    docker buildx imagetools create \
        --tag "${REGISTRY}/${image}:${tag}" \
        "${arch_images[@]}"
}

create_manifest_if_available() {
    local image="$1"
    local tag="$2"
    local arch_pattern="$3"

    for arch in $BUILD_ARCHS; do
        arch_image="${REGISTRY}/${image}:${arch_pattern//\{arch\}/$arch}"
        if ! docker buildx imagetools inspect "$arch_image" >/dev/null 2>&1; then
            echo "Skipping ${image}:${tag}; ${arch_image} was not published"
            return 0
        fi
    done

    create_manifest "$image" "$tag" "$arch_pattern"
}

for image in "${IMAGES[@]}"; do
    create_manifest "$image" "$BUILD_NUMBER" "${BUILD_NUMBER}-linux-{arch}"
done

for image in "${BASE_IMAGES[@]}"; do
    create_manifest_if_available "$image" "$BUILD_NUMBER" "${BUILD_NUMBER}-{arch}"
done
create_manifest_if_available \
    "native-build" "$BUILD_NUMBER" "${BUILD_NUMBER}-linux-{arch}"

if [[ "$PUBLISH_LATEST" == "true" ]]; then
    for image in "${IMAGES[@]}"; do
        create_manifest "$image" "latest" "${BUILD_NUMBER}-linux-{arch}"
    done
    for image in "${BASE_IMAGES[@]}"; do
        create_manifest "$image" "latest" "latest-linux-{arch}"
    done
    create_manifest "native-build" "latest" "linux-{arch}"
fi

echo "Published multi-platform manifests for ${BUILD_NUMBER}"
