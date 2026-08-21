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
# PR builds rebuild only when native-build inputs changed and publish only an
# immutable build tag. Official main builds always rebuild and additionally
# update the rolling architecture tag.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
BUILD_NUMBER="${BUILD_NUMBER:?BUILD_NUMBER is required}"
REGISTRY="${DOCKER_REGISTRY:?DOCKER_REGISTRY is required}"
PUBLISH_LATEST="${PUBLISH_LATEST:-false}"
PUBLISH_ONLY="${PUBLISH_ONLY:-false}"
PUSH_IMAGES="${PUSH_IMAGES:-true}"
USE_AZURE_CFS="${USE_AZURE_CFS:-false}"
UBUNTU_ARCHIVE_MIRROR="${UBUNTU_ARCHIVE_MIRROR:-}"

if [[ "$USE_AZURE_CFS" == "true" && -z "$UBUNTU_ARCHIVE_MIRROR" ]]; then
    UBUNTU_ARCHIVE_MIRROR="http://azure.archive.ubuntu.com/ubuntu"
fi

IMAGE_NAME="native-build"
ROLLING_IMAGE="${REGISTRY}/${IMAGE_NAME}:linux-${BUILD_ARCH}"
IMMUTABLE_IMAGE="${REGISTRY}/${IMAGE_NAME}:${BUILD_NUMBER}-linux-${BUILD_ARCH}"

publish_image() {
    local image_sha
    image_sha="$(docker inspect "$ROLLING_IMAGE" \
        --format '{{ index .Config.Labels "git-sha"}}' 2>/dev/null || true)"
    if [[ "$image_sha" != "$(git rev-parse HEAD)" ]]; then
        echo "Native-build image is unchanged; no immutable tag to publish"
        return
    fi

    docker tag "$ROLLING_IMAGE" "$IMMUTABLE_IMAGE"
    docker push "$IMMUTABLE_IMAGE"

    if [[ "$PUBLISH_LATEST" == "true" ]]; then
        docker push "$ROLLING_IMAGE"
    fi
}

if [[ "$PUBLISH_ONLY" == "true" ]]; then
    publish_image
    exit 0
fi

native_build_changed() {
    if [[ "$PUBLISH_LATEST" == "true" ]]; then
        echo "[DEBUG] Official main build: forcing native-build refresh" >&2
        return 0
    fi

    git fetch origin main --depth=1 2>/dev/null || true
    if git diff --name-only origin/main..HEAD 2>/dev/null |
        grep -q '^src/images/native-build/'; then
        echo "[DEBUG] src/images/native-build/ changed relative to origin/main" >&2
        return 0
    fi

    echo "[DEBUG] Native-build inputs are unchanged" >&2
    return 1
}

if ! native_build_changed; then
    echo "Native-build image unchanged; pulling the official main image"
    docker pull "$ROLLING_IMAGE"
    exit 0
fi

echo "Building native-build image for linux/${BUILD_ARCH}"

build_args=(--pull)
pip_build_secret=()
if [[ "$PUBLISH_LATEST" == "true" ]]; then
    build_args+=(--no-cache)
fi
if [[ -n "$UBUNTU_ARCHIVE_MIRROR" ]]; then
    build_args+=(--build-arg "UBUNTU_ARCHIVE_MIRROR=$UBUNTU_ARCHIVE_MIRROR")
fi
if [[ "$USE_AZURE_CFS" == "true" && -n "${PIP_INDEX_URL:-}" ]]; then
    pip_build_secret=(--secret "id=pip_index_url,env=PIP_INDEX_URL")
fi

docker build \
    "${build_args[@]}" \
    "${pip_build_secret[@]}" \
    --build-arg "TARGETARCH=$BUILD_ARCH" \
    --label "git-sha=$(git rev-parse HEAD)" \
    -t "$ROLLING_IMAGE" \
    -f "$REPOSITORY_ROOT/src/images/native-build/Dockerfile" \
    "$REPOSITORY_ROOT/src/images/native-build"

if [[ "$PUSH_IMAGES" == "true" ]]; then
    publish_image
fi
