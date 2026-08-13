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

# Build an architecture-specific DACS Rust client crate without publishing it.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR
REPOSITORY_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
readonly REPOSITORY_ROOT
readonly BUILD_ARCH="${BUILD_ARCHITECTURE:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
readonly BUILD_CONFIG="${BUILD_CONFIGURATION:-Release}"
readonly CRATE_DIR="${REPOSITORY_ROOT}/src/subsystems/storage/rust/dacs_client"

case "${BUILD_ARCH}" in
  amd64)
    readonly NATIVE_DIR="linux-x86_64"
    ;;
  arm64)
    readonly NATIVE_DIR="linux-aarch64"
    ;;
  *)
    echo "Unsupported Rust package architecture: ${BUILD_ARCH}" >&2
    exit 1
    ;;
esac

readonly LIBRARY_SOURCE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIG}/${BUILD_ARCH}/native-storage/storage/libStorageDirect.so"
readonly NATIVE_STAGE_DIR="${CRATE_DIR}/native/${NATIVE_DIR}"
BACKUP_DIR="$(mktemp -d)"
readonly BACKUP_DIR
HAD_NATIVE_STAGE=0

cleanup() {
  rm -rf -- "${NATIVE_STAGE_DIR}"
  if [[ "${HAD_NATIVE_STAGE}" -eq 1 ]]; then
    mkdir -p -- "$(dirname "${NATIVE_STAGE_DIR}")"
    mv -- "${BACKUP_DIR}/native-stage" "${NATIVE_STAGE_DIR}"
  fi
  rm -rf -- "${BACKUP_DIR}"
}

trap cleanup EXIT

if [[ ! -f "${LIBRARY_SOURCE}" ]]; then
  echo "Native library not found: ${LIBRARY_SOURCE}" >&2
  exit 1
fi

if [[ -e "${NATIVE_STAGE_DIR}" ]]; then
  mv -- "${NATIVE_STAGE_DIR}" "${BACKUP_DIR}/native-stage"
  HAD_NATIVE_STAGE=1
fi

echo "Staging ${LIBRARY_SOURCE}..."
mkdir -p -- "${NATIVE_STAGE_DIR}"
cp -- "${LIBRARY_SOURCE}" "${NATIVE_STAGE_DIR}/libStorageDirect.so"

rm -f -- "${CRATE_DIR}"/target/package/*.crate

echo "Packaging Rust client for ${BUILD_ARCH}..."
cargo package \
  --allow-dirty \
  --locked \
  --manifest-path "${CRATE_DIR}/Cargo.toml" \
  --no-verify

CRATE_FILE="$(find "${CRATE_DIR}/target/package" -maxdepth 1 -type f -name '*.crate' -print -quit)"
if [[ -z "${CRATE_FILE}" ]]; then
  echo "Cargo did not produce a crate under ${CRATE_DIR}/target/package" >&2
  exit 1
fi

if ! tar -tzf "${CRATE_FILE}" |
  grep -E "^[^/]+/native/${NATIVE_DIR}/libStorageDirect\\.so$" >/dev/null; then
  echo "Crate is missing native/${NATIVE_DIR}/libStorageDirect.so: ${CRATE_FILE}" >&2
  exit 1
fi

echo "Rust client crate ready: ${CRATE_FILE}"
