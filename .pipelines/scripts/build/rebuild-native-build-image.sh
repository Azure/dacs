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

# Rebuild native-build container image if its Dockerfile or dependencies changed
#
# For official builds (main branch): builds and pushes the updated image to ACR
# For PR builds: builds the updated image locally for use in bldenv.sh, but does not push to ACR
#
# Required environment variables:
#   BUILD_ARCH           - Target architecture (amd64 or arm64)
#
# Optional environment variables:
#   SHOULD_PUBLISH_IMAGES - Set to "True" for official builds (triggers push to ACR)
#   DOCKER_REGISTRY       - ACR registry (e.g. dacstest.azurecr.io)
#   BUILD_SOURCEBRANCHNAME - Set by Azure Pipelines (e.g. "main")
#   USE_AZURE_CFS         - Set to "true" to use Azure's Ubuntu package mirror
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
USE_AZURE_CFS="${USE_AZURE_CFS:-false}"
UBUNTU_ARCHIVE_MIRROR="${UBUNTU_ARCHIVE_MIRROR:-}"
if [ "${USE_AZURE_CFS}" = "true" ] && [ -z "${UBUNTU_ARCHIVE_MIRROR}" ]; then
    UBUNTU_ARCHIVE_MIRROR="http://azure.archive.ubuntu.com/ubuntu"
fi

# Legacy ACR used by bldenv.sh
LEGACY_ACR="dacstest.azurecr.io"
IMAGE_NAME="native-build"
IMAGE_TAG="linux-${BUILD_ARCH}"
ACR_IMAGE="${LEGACY_ACR}/${IMAGE_NAME}:${IMAGE_TAG}"

# Detect if native-build Dockerfile or its dependencies changed
native_build_changed() {
    # Fetch origin/main tip for comparison
    git fetch origin main --depth=1 2>/dev/null || true

    # Compare against origin/main (for PR builds)
    # Use two-dot diff (direct tip comparison) to work with shallow clones
    if git diff --name-only origin/main..HEAD 2>/dev/null | grep -q "src/images/native-build/"; then
        echo "[DEBUG] src/images/native-build/ changed (vs origin/main)" >&2
        return 0
    fi

    # Compare against previous commit (for official builds on main)
    if git diff --name-only HEAD~1 2>/dev/null | grep -q "src/images/native-build/"; then
        echo "[DEBUG] src/images/native-build/ changed (vs HEAD~1)" >&2
        return 0
    fi

    echo "[DEBUG] No src/images/native-build/ changes detected" >&2
    return 1
}

if ! native_build_changed; then
    echo "Native-build image unchanged, skipping rebuild"
    exit 0
fi

# Image changed — always rebuild locally so bldenv.sh picks it up
echo "Native-build image changed — rebuilding locally..."
echo "  Architecture: ${BUILD_ARCH}"
echo "  Image: ${ACR_IMAGE}"

pip_build_secret=()
native_build_args=()
if [ -n "${UBUNTU_ARCHIVE_MIRROR}" ]; then
    native_build_args+=(--build-arg "UBUNTU_ARCHIVE_MIRROR=${UBUNTU_ARCHIVE_MIRROR}")
fi
if [ "${USE_AZURE_CFS}" = "true" ] && [ -n "${PIP_INDEX_URL:-}" ]; then
    pip_build_secret=(--secret "id=pip_index_url,env=PIP_INDEX_URL")
fi

docker build \
    "${native_build_args[@]}" \
    "${pip_build_secret[@]}" \
    --build-arg TARGETARCH="${BUILD_ARCH}" \
    -t "${ACR_IMAGE}" \
    -f "${REPOSITORY_ROOT}/src/images/native-build/Dockerfile" \
    "${REPOSITORY_ROOT}/src/images/native-build"

echo "✓ Native-build image rebuilt locally"

# Only push to ACR on official builds (main branch with publish enabled)
is_official_build() {
    [ "${SHOULD_PUBLISH_IMAGES}" = "True" ] && [ "${BUILD_SOURCEBRANCHNAME}" = "main" ]
}

if ! is_official_build; then
    echo "PR build — skipping push (image available locally for bldenv.sh)"
    exit 0
fi

echo "Official build — pushing to ACR..."

echo "Pushing ${ACR_IMAGE}..."
docker push "${ACR_IMAGE}"

# Also push to target ACR if set and different from legacy
if [ -n "${DOCKER_REGISTRY}" ] && [ "${DOCKER_REGISTRY}" != "${LEGACY_ACR}" ]; then
    TARGET_IMAGE="${DOCKER_REGISTRY}/${IMAGE_NAME}:${IMAGE_TAG}"
    echo "Pushing ${TARGET_IMAGE}..."
    docker tag "${ACR_IMAGE}" "${TARGET_IMAGE}"
    docker push "${TARGET_IMAGE}"
fi

echo "✓ Native-build image pushed to ACR"
