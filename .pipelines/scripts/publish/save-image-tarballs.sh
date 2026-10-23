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

# Save locally-built Docker images as gzipped tarballs for offline distribution.
#
# Expected upstream state:
#   make images (or build-docker-images.sh) has produced <image>:local for each
#   image listed below. Run this BEFORE publish-images.sh, which removes the
#   local tags after pushing.
#
# Output:
#   ${OUTPUT_DIR}/<image>-${BUILD_NUMBER}-linux-${BUILD_ARCH}.tar.gz   (one per image)
#   ${OUTPUT_DIR}/SHA256SUMS                                            (sha256 manifest)
#
# Each tarball is saved with the same ref it is published under in ACR
# (<REGISTRY>/<image>:<BUILD_NUMBER>-linux-<BUILD_ARCH>) so `docker load`
# restores an image that lines up with the helm chart defaults.
#
# Required environment variables:
#   DOCKER_REGISTRY   - Target registry (e.g. dacstest.azurecr.io)
#   BUILD_NUMBER      - Build number used in the tag
#
# Optional environment variables:
#   BUILD_ARCH        - Target arch (default: host arch normalized to amd64/arm64)
#   OUTPUT_DIR        - Output directory (default: <repo>/image-tarballs)
#   GZIP_LEVEL        - gzip compression level 1-9 (default: 6)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

if [[ -z "${DOCKER_REGISTRY:-}" ]]; then
    echo "ERROR: DOCKER_REGISTRY must be set" >&2
    exit 1
fi
if [[ -z "${BUILD_NUMBER:-}" ]]; then
    echo "ERROR: BUILD_NUMBER must be set" >&2
    exit 1
fi

BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
OUTPUT_DIR="${OUTPUT_DIR:-$REPOSITORY_ROOT/image-tarballs}"
GZIP_LEVEL="${GZIP_LEVEL:-6}"

# Must match IMAGES in publish-images.sh / build-docker-images.sh
IMAGES=("cache-server" "cache-test-client" "cacheserver-scheduler" "cache-controller" "cache-setup")

echo "Saving Docker images as tarballs"
echo "  Registry     : ${DOCKER_REGISTRY}"
echo "  Build number : ${BUILD_NUMBER}"
echo "  Architecture : ${BUILD_ARCH}"
echo "  Output dir   : ${OUTPUT_DIR}"
echo ""

mkdir -p "$OUTPUT_DIR"
: > "$OUTPUT_DIR/SHA256SUMS"

for image in "${IMAGES[@]}"; do
    src="${image}:local"
    dst="${DOCKER_REGISTRY}/${image}:${BUILD_NUMBER}-linux-${BUILD_ARCH}"
    out="${OUTPUT_DIR}/${image}-${BUILD_NUMBER}-linux-${BUILD_ARCH}.tar.gz"

    if ! docker image inspect "$src" >/dev/null 2>&1; then
        echo "ERROR: Local image '$src' not found. Run 'make images' first." >&2
        exit 1
    fi

    echo "Tagging $src -> $dst"
    docker tag "$src" "$dst"

    echo "Saving  $dst -> $out"
    docker save "$dst" | gzip "-${GZIP_LEVEL}" > "$out"

    size=$(du -h "$out" | awk '{print $1}')
    sha=$(sha256sum "$out" | awk '{print $1}')
    echo "  ${size}  sha256:${sha}"
    echo "${sha}  $(basename "$out")" >> "$OUTPUT_DIR/SHA256SUMS"
done

echo ""
echo "Image tarballs written to: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR"
echo ""
echo "Manifest (SHA256SUMS):"
cat "$OUTPUT_DIR/SHA256SUMS"
