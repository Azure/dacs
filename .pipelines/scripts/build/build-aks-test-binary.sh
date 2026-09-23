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
readonly SCRIPT_DIR
REPOSITORY_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
readonly REPOSITORY_ROOT
BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
readonly BUILD_ARCH
BUILD_TYPE="${BUILD_TYPE:-Debug}"
readonly BUILD_TYPE
OUTPUT="${AKS_TEST_BINARY_OUTPUT:-${REPOSITORY_ROOT}/bin/${BUILD_TYPE}/${BUILD_ARCH}/aks-tests/dacs-aks-tests}"
if [[ "${OUTPUT}" != /* ]]; then
    OUTPUT="${REPOSITORY_ROOT}/${OUTPUT}"
fi
readonly OUTPUT

case "${BUILD_ARCH}" in
    amd64 | arm64) ;;
    *)
        echo "Unsupported AKS test binary architecture: ${BUILD_ARCH}" >&2
        exit 1
        ;;
esac

mkdir -p -- "$(dirname "${OUTPUT}")"

(
    cd "${REPOSITORY_ROOT}"
    CGO_ENABLED=0 GOOS=linux GOARCH="${BUILD_ARCH}" \
        go test -c -trimpath -o "${OUTPUT}" ./test/aks
)

chmod 0755 "${OUTPUT}"
echo "AKS test binary ready: ${OUTPUT}"
