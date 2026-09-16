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

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

cd "$REPOSITORY_ROOT"

mapfile -t images < <(
    while IFS= read -r dockerfile; do
        awk '
            toupper($1) == "FROM" {
                image_field = 2
                while ($image_field ~ /^--/) {
                    image_field++
                }

                image = $image_field
                if (image ~ /^mcr\.microsoft\.com\//) {
                    print image
                }
            }
        ' "$dockerfile"
    done < <(git ls-files -- '*Dockerfile' '*.Dockerfile' | sort -u)
)

if ((${#images[@]} == 0)); then
    echo "No MCR base images were found." >&2
    exit 1
fi

for image in "${images[@]}"; do
    if [[ "$image" != *@sha256:* ]]; then
        echo "MCR base image is not pinned by digest: $image" >&2
        exit 1
    fi

    digest="${image##*@sha256:}"
    if [[ ! "$digest" =~ ^[0-9a-f]{64}$ ]]; then
        echo "MCR base image has an invalid SHA-256 digest: $image" >&2
        exit 1
    fi
done

printf '%s\n' "${images[@]}" | sort -u
