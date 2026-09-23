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
readonly AMD64_ARTIFACT_ROOT="${1:?amd64 build artifact root is required}"
readonly ARM64_ARTIFACT_ROOT="${2:?arm64 build artifact root is required}"
readonly OUTPUT_ROOT="${3:?output root is required}"
RUST_VERSION="$("${REPOSITORY_ROOT}/.pipelines/scripts/get-version.sh" rust)"
readonly RUST_VERSION
readonly RUST_SOURCE="${REPOSITORY_ROOT}/src/subsystems/storage/rust/dacs_client"
readonly RUST_BUNDLE_NAME="dacs-rust-client-${RUST_VERSION}-publish"
readonly RUST_BUNDLE_ROOT="${OUTPUT_ROOT}/rust/${RUST_BUNDLE_NAME}"
TEMP_DIR=""

cleanup() {
    if [[ -n "${TEMP_DIR}" && -d "${TEMP_DIR}" ]]; then
        rm -rf -- "${TEMP_DIR}"
    fi
}
trap cleanup EXIT

extract_native_packages() {
    local architecture="$1"
    local artifact_root="$2"
    local archive="${artifact_root}/build-tree/dacs-build-tree-linux-${architecture}.tar"
    local archive_root="bin/Release/${architecture}"
    local package_path
    local package_name
    local destination
    local extract_root="${TEMP_DIR}/${architecture}"
    local archive_listing="${extract_root}/archive.list"

    [[ -f "${archive}" ]] || {
        echo "Native build archive not found: ${archive}" >&2
        return 1
    }

    mkdir -p -- "${extract_root}"
    tar -tf "${archive}" > "${archive_listing}"
    for package_name in native-storage native-storage-cache-client aks-tests; do
        package_path="${archive_root}/${package_name}"
        grep -q "^${package_path}/" "${archive_listing}" || {
            echo "Native build archive is missing ${package_path}" >&2
            return 1
        }
        tar -xf "${archive}" -C "${extract_root}" "${package_path}"
        destination="${OUTPUT_ROOT}/universal/${package_name}/${architecture}"
        mkdir -p -- "${destination}"
        cp -a -- "${extract_root}/${package_path}/." "${destination}/"
    done

    test -f \
        "${OUTPUT_ROOT}/universal/native-storage/${architecture}/storage/libStorageDirect.so"
    test -x \
        "${OUTPUT_ROOT}/universal/aks-tests/${architecture}/dacs-aks-tests"
}

copy_wheels() {
    local artifact_root
    local wheel
    local wheel_count=0

    mkdir -p -- "${OUTPUT_ROOT}/python"
    for artifact_root in "${AMD64_ARTIFACT_ROOT}" "${ARM64_ARTIFACT_ROOT}"; do
        while IFS= read -r -d '' wheel; do
            cp -- "${wheel}" "${OUTPUT_ROOT}/python/"
            wheel_count=$((wheel_count + 1))
        done < <(find "${artifact_root}/python-wheel" -maxdepth 1 -type f -name '*.whl' -print0)
    done

    [[ "${wheel_count}" -eq 2 ]] || {
        echo "Expected two Python wheels, found ${wheel_count}." >&2
        return 1
    }
}

prepare_rust_bundle() {
    local architecture
    local native_dir
    local native_source

    mkdir -p -- "${RUST_BUNDLE_ROOT}"
    tar \
        --exclude='./target' \
        --exclude='./native' \
        -C "${RUST_SOURCE}" \
        -cf - . |
        tar -C "${RUST_BUNDLE_ROOT}" -xf -

    cp -- "${REPOSITORY_ROOT}/LICENSE" "${RUST_BUNDLE_ROOT}/LICENSE"
    cp -- "${REPOSITORY_ROOT}/NOTICE.txt" "${RUST_BUNDLE_ROOT}/NOTICE.txt"

    sed -i \
        "/^\[package\]/,/^\[/ s/^version = \".*\"/version = \"${RUST_VERSION}\"/" \
        "${RUST_BUNDLE_ROOT}/Cargo.toml"
    sed -i \
        "/^name = \"dacs-rust-client\"$/,/^$/ s/^version = \".*\"/version = \"${RUST_VERSION}\"/" \
        "${RUST_BUNDLE_ROOT}/Cargo.lock"

    for architecture in amd64 arm64; do
        case "${architecture}" in
            amd64)
                native_dir="linux-x86_64"
                ;;
            arm64)
                native_dir="linux-aarch64"
                ;;
        esac
        native_source="${OUTPUT_ROOT}/universal/native-storage/${architecture}/storage/libStorageDirect.so"
        mkdir -p -- "${RUST_BUNDLE_ROOT}/native/${native_dir}"
        cp -- "${native_source}" "${RUST_BUNDLE_ROOT}/native/${native_dir}/"
    done

    grep -q '^publish = false$' "${RUST_BUNDLE_ROOT}/Cargo.toml"
    test -f "${RUST_BUNDLE_ROOT}/native/linux-x86_64/libStorageDirect.so"
    test -f "${RUST_BUNDLE_ROOT}/native/linux-aarch64/libStorageDirect.so"

    tar -C "${OUTPUT_ROOT}/rust" -czf \
        "${OUTPUT_ROOT}/rust/${RUST_BUNDLE_NAME}.tar.gz" \
        "${RUST_BUNDLE_NAME}"
    tar -tzf "${OUTPUT_ROOT}/rust/${RUST_BUNDLE_NAME}.tar.gz" \
        > "${TEMP_DIR}/rust-package.list"
    grep -q "${RUST_BUNDLE_NAME}/native/linux-aarch64/libStorageDirect.so" \
        "${TEMP_DIR}/rust-package.list"
    grep -q "${RUST_BUNDLE_NAME}/native/linux-x86_64/libStorageDirect.so" \
        "${TEMP_DIR}/rust-package.list"
    rm -rf -- "${RUST_BUNDLE_ROOT}"
}

main() {
    rm -rf -- "${OUTPUT_ROOT}"
    mkdir -p -- "${OUTPUT_ROOT}"
    TEMP_DIR="$(mktemp -d)"

    extract_native_packages amd64 "${AMD64_ARTIFACT_ROOT}"
    extract_native_packages arm64 "${ARM64_ARTIFACT_ROOT}"
    copy_wheels
    prepare_rust_bundle

    (
        cd "${OUTPUT_ROOT}"
        find universal python rust -type f -print0 |
            sort -z |
            xargs -0 sha256sum > SHA256SUMS
    )
}

main
