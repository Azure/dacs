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
readonly SCRIPT="${SCRIPT_DIR}/../build/prepare-package-artifacts.sh"
TEST_DIR=""

cleanup() {
    if [[ -n "${TEST_DIR}" && -d "${TEST_DIR}" ]]; then
        rm -rf -- "${TEST_DIR}"
    fi
}
trap cleanup EXIT

TEST_DIR="$(mktemp -d)"

for architecture in amd64 arm64; do
    artifact_root="${TEST_DIR}/${architecture}"
    archive_root="${TEST_DIR}/archive-${architecture}"
    mkdir -p \
        "${artifact_root}/build-tree" \
        "${artifact_root}/python-wheel" \
        "${archive_root}/bin/Release/${architecture}/native-storage/storage" \
        "${archive_root}/bin/Release/${architecture}/native-storage-cache-client/lib" \
        "${archive_root}/bin/Release/${architecture}/aks-tests"

    printf 'storage-%s\n' "${architecture}" \
        > "${archive_root}/bin/Release/${architecture}/native-storage/storage/libStorageDirect.so"
    printf 'client-%s\n' "${architecture}" \
        > "${archive_root}/bin/Release/${architecture}/native-storage-cache-client/lib/libCacheClientSharedLib.so"
    printf '#!/bin/sh\nexit 0\n' \
        > "${archive_root}/bin/Release/${architecture}/aks-tests/dacs-aks-tests"
    chmod 0755 \
        "${archive_root}/bin/Release/${architecture}/aks-tests/dacs-aks-tests"

    tar -C "${archive_root}" -cf \
        "${artifact_root}/build-tree/dacs-build-tree-linux-${architecture}.tar" \
        "bin/Release/${architecture}/native-storage" \
        "bin/Release/${architecture}/native-storage-cache-client" \
        "bin/Release/${architecture}/aks-tests"

    printf 'wheel-%s\n' "${architecture}" \
        > "${artifact_root}/python-wheel/dacs_client-0.1.0-py3-none-manylinux_${architecture}.whl"
done

readonly OUTPUT_ROOT="${TEST_DIR}/output"
"${SCRIPT}" "${TEST_DIR}/amd64" "${TEST_DIR}/arm64" "${OUTPUT_ROOT}"

for architecture in amd64 arm64; do
    test -f \
        "${OUTPUT_ROOT}/universal/native-storage/${architecture}/storage/libStorageDirect.so"
    test -f \
        "${OUTPUT_ROOT}/universal/native-storage-cache-client/${architecture}/lib/libCacheClientSharedLib.so"
    test -x \
        "${OUTPUT_ROOT}/universal/aks-tests/${architecture}/dacs-aks-tests"
done

test "$(find "${OUTPUT_ROOT}/python" -maxdepth 1 -type f -name '*.whl' | wc -l)" -eq 2
test -n "$(find "${OUTPUT_ROOT}/rust" -maxdepth 1 -type f -name '*.tar.gz' -print -quit)"

(
    cd "${OUTPUT_ROOT}"
    sha256sum -c SHA256SUMS --quiet
)
