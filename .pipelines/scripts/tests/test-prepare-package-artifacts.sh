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
    platform_arch=x86_64
    if [[ "$architecture" == arm64 ]]; then
        platform_arch=aarch64
    fi
    artifact_root="${TEST_DIR}/${architecture}"
    focal_artifact_root="${TEST_DIR}/focal-${architecture}"
    archive_root="${TEST_DIR}/archive-${architecture}"
    mkdir -p \
        "${artifact_root}/build-tree" \
        "${artifact_root}/python-wheel" \
        "${focal_artifact_root}/python-wheel" \
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
        > "${artifact_root}/python-wheel/dacs_client-0.1.0-py3-none-manylinux_2_35_${platform_arch}.whl"
    printf 'focal-wheel-%s\n' "${architecture}" \
        > "${focal_artifact_root}/python-wheel/dacs_client-0.1.0-py3-none-manylinux_2_31_${platform_arch}.whl"
done

readonly OUTPUT_ROOT="${TEST_DIR}/output"
package_args=(
    "${TEST_DIR}/amd64" "${TEST_DIR}/arm64" "${OUTPUT_ROOT}"
    "${TEST_DIR}/focal-amd64" "${TEST_DIR}/focal-arm64"
)
"${SCRIPT}" "${package_args[@]}"

for architecture in amd64 arm64; do
    test -f \
        "${OUTPUT_ROOT}/universal/native-storage/${architecture}/storage/libStorageDirect.so"
    test -f \
        "${OUTPUT_ROOT}/universal/native-storage-cache-client/${architecture}/lib/libCacheClientSharedLib.so"
    test -x \
        "${OUTPUT_ROOT}/universal/aks-tests/${architecture}/dacs-aks-tests"
done

test "$(find "${OUTPUT_ROOT}/python" -maxdepth 1 -type f -name '*.whl' | wc -l)" -eq 4
test -n "$(find "${OUTPUT_ROOT}/rust" -maxdepth 1 -type f -name '*.tar.gz' -print -quit)"

(
    cd "${OUTPUT_ROOT}"
    sha256sum -c SHA256SUMS --quiet
)

readonly FOCAL_WHEEL="${TEST_DIR}/focal-amd64/python-wheel/dacs_client-0.1.0-py3-none-manylinux_2_31_x86_64.whl"
mv "${FOCAL_WHEEL}" "${TEST_DIR}/saved.whl"
if "${SCRIPT}" "${package_args[@]}"; then
    echo "Package assembly accepted a missing Focal wheel." >&2
    exit 1
fi
mv "${TEST_DIR}/saved.whl" "${FOCAL_WHEEL}"

cp "${FOCAL_WHEEL}" "${TEST_DIR}/focal-amd64/python-wheel/extra.whl"
if "${SCRIPT}" "${package_args[@]}"; then
    echo "Package assembly accepted multiple wheels for one build." >&2
    exit 1
fi
rm "${TEST_DIR}/focal-amd64/python-wheel/extra.whl"

mv "${FOCAL_WHEEL}" \
    "${TEST_DIR}/focal-amd64/python-wheel/dacs_client-0.1.0-py3-none-manylinux_2_35_x86_64.whl"
if "${SCRIPT}" "${package_args[@]}"; then
    echo "Package assembly accepted colliding wheel filenames." >&2
    exit 1
fi
