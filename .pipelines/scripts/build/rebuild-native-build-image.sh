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

# Prepare the architecture-specific native build image used by bldenv.sh.
#
# BuildKit uses the registry cache to reuse unchanged layers while ensuring the
# locally loaded image always matches the current native-build inputs.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/build-cache.sh"

BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
REGISTRY="${DOCKER_REGISTRY:?DOCKER_REGISTRY is required}"
BUILDKIT_IMAGE="${BUILDKIT_IMAGE:-mcr.microsoft.com/oss/v2/moby/buildkit:v0.31.0@sha256:fe54b035f7e18247c19d74adfaae42f62f44dca1fd00ae35b795a662a6126d67}"
BUILD_CACHE_SCOPE="$(resolve_build_cache_scope "${BUILD_CACHE_SCOPE:-main}")"
USE_AZURE_CFS="${USE_AZURE_CFS:-false}"
UBUNTU_ARCHIVE_MIRROR="${UBUNTU_ARCHIVE_MIRROR:-}"

if [[ "$USE_AZURE_CFS" == "true" && -z "$UBUNTU_ARCHIVE_MIRROR" ]]; then
    UBUNTU_ARCHIVE_MIRROR="http://azure.archive.ubuntu.com/ubuntu"
fi

IMAGE_NAME="dacs/native-build"
ROLLING_IMAGE="${REGISTRY}/${IMAGE_NAME}:linux-${BUILD_ARCH}"
GIT_SHA="$(git -C "$REPOSITORY_ROOT" rev-parse HEAD)"
IMMUTABLE_IMAGE="${REGISTRY}/${IMAGE_NAME}:git-${GIT_SHA}-linux-${BUILD_ARCH}"
CACHE_IMAGE="${REGISTRY}/${IMAGE_NAME}:buildcache-${BUILD_CACHE_SCOPE}-linux-${BUILD_ARCH}"
MAIN_CACHE_IMAGE="${REGISTRY}/${IMAGE_NAME}:buildcache-main-linux-${BUILD_ARCH}"
LEGACY_CACHE_IMAGE="${REGISTRY}/${IMAGE_NAME}:buildcache-linux-${BUILD_ARCH}"

echo "Checking for existing native-build image: ${IMMUTABLE_IMAGE}"
if docker pull "$IMMUTABLE_IMAGE"; then
    docker tag "$IMMUTABLE_IMAGE" "$ROLLING_IMAGE"
    echo "Using existing native-build image for commit ${GIT_SHA}"
    exit 0
fi

echo "Building native-build image for linux/${BUILD_ARCH}"

BUILDER_NAME="dacs-native-build-${BUILD_ARCH}-$$"
cleanup() {
    docker buildx rm "$BUILDER_NAME" >/dev/null 2>&1 || true
}
trap cleanup EXIT

docker buildx create \
    --name "$BUILDER_NAME" \
    --driver docker-container \
    --driver-opt "image=${BUILDKIT_IMAGE}" \
    --use >/dev/null
docker buildx inspect --bootstrap >/dev/null

build_args=(
    --pull
    --platform "linux/${BUILD_ARCH}"
    --cache-from "type=registry,ref=${CACHE_IMAGE}"
    --cache-to "type=registry,ref=${CACHE_IMAGE},mode=min"
    --load
)
if [[ "$CACHE_IMAGE" != "$MAIN_CACHE_IMAGE" ]]; then
    build_args+=(--cache-from "type=registry,ref=${MAIN_CACHE_IMAGE}")
fi
build_args+=(--cache-from "type=registry,ref=${LEGACY_CACHE_IMAGE}")
pip_build_secret=()
if [[ -n "$UBUNTU_ARCHIVE_MIRROR" ]]; then
    build_args+=(--build-arg "UBUNTU_ARCHIVE_MIRROR=$UBUNTU_ARCHIVE_MIRROR")
fi
if [[ "$USE_AZURE_CFS" == "true" && -n "${PIP_INDEX_URL:-}" ]]; then
    pip_build_secret=(--secret "id=pip_index_url,env=PIP_INDEX_URL")
fi

docker buildx build \
    "${build_args[@]}" \
    "${pip_build_secret[@]}" \
    --build-arg "TARGETARCH=$BUILD_ARCH" \
    --label "git-sha=${GIT_SHA}" \
    -t "$ROLLING_IMAGE" \
    -t "$IMMUTABLE_IMAGE" \
    -f "$REPOSITORY_ROOT/src/images/native-build/Dockerfile" \
    "$REPOSITORY_ROOT/src/images/native-build"
