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

# Verify that every image referenced by the published Helm charts exists.
set -euo pipefail

REGISTRY="${DOCKER_REGISTRY:?DOCKER_REGISTRY is required}"
BUILD_NUMBER="${BUILD_NUMBER:?BUILD_NUMBER is required}"
IMAGE_REGISTRY="${IMAGE_REGISTRY:-${REGISTRY%/}/dacs}"
IMAGES=("cache-server" "cacheserver-scheduler" "cache-controller")

for image in "${IMAGES[@]}"; do
    image_ref="${IMAGE_REGISTRY}/${image}:${BUILD_NUMBER}"
    if ! docker buildx imagetools inspect "${image_ref}" >/dev/null; then
        echo "Required Helm chart image does not exist: ${image_ref}" >&2
        exit 1
    fi
done

echo "Verified Helm chart images for ${BUILD_NUMBER}"
