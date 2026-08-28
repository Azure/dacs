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
readonly LICENSE_SOURCE="${REPOSITORY_ROOT}/LICENSE"
readonly NOTICE_SOURCE="${REPOSITORY_ROOT}/NOTICE.txt"
readonly LICENSE_STAGE="${CRATE_DIR}/LICENSE"
readonly NOTICE_STAGE="${CRATE_DIR}/NOTICE.txt"
readonly MANIFEST_FILE="${CRATE_DIR}/Cargo.toml"
readonly LOCK_FILE="${CRATE_DIR}/Cargo.lock"
readonly TARGET_DIR="${CARGO_TARGET_DIR:-${CRATE_DIR}/target}"
RUST_VERSION="$("${REPOSITORY_ROOT}/.pipelines/scripts/get-version.sh" rust)"
readonly RUST_VERSION

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

if [[ ! -f "${LIBRARY_SOURCE}" ]]; then
  echo "Native library not found: ${LIBRARY_SOURCE}" >&2
  exit 1
fi

for source_file in "${LICENSE_SOURCE}" "${NOTICE_SOURCE}"; do
  if [[ ! -f "${source_file}" ]]; then
    echo "Required compliance file not found: ${source_file}" >&2
    exit 1
  fi
done

if [[ -e "${LICENSE_STAGE}" || -e "${NOTICE_STAGE}" ]]; then
  echo "Refusing to overwrite staged crate compliance files" >&2
  exit 1
fi

BACKUP_DIR="$(mktemp -d "${CRATE_DIR}/.dacs-rust-package.XXXXXX")"
readonly BACKUP_DIR
HAD_NATIVE_STAGE=0
STAGED_COMPLIANCE_FILES=()

cleanup() {
  local staged_file

  rm -rf -- "${NATIVE_STAGE_DIR}"
  for staged_file in "${STAGED_COMPLIANCE_FILES[@]}"; do
    rm -f -- "${staged_file}"
  done
  cp -- "${BACKUP_DIR}/Cargo.toml" "${MANIFEST_FILE}"
  cp -- "${BACKUP_DIR}/Cargo.lock" "${LOCK_FILE}"
  if [[ "${HAD_NATIVE_STAGE}" -eq 1 ]]; then
    mkdir -p -- "$(dirname "${NATIVE_STAGE_DIR}")"
    mv -- "${BACKUP_DIR}/native-stage" "${NATIVE_STAGE_DIR}"
  fi
  rm -rf -- "${BACKUP_DIR}"
}

cp -- "${MANIFEST_FILE}" "${BACKUP_DIR}/Cargo.toml"
cp -- "${LOCK_FILE}" "${BACKUP_DIR}/Cargo.lock"

if [[ -e "${NATIVE_STAGE_DIR}" ]]; then
  mv -- "${NATIVE_STAGE_DIR}" "${BACKUP_DIR}/native-stage"
  HAD_NATIVE_STAGE=1
fi

trap cleanup EXIT

echo "Staging ${LIBRARY_SOURCE}..."
mkdir -p -- "${NATIVE_STAGE_DIR}"
cp -- "${LIBRARY_SOURCE}" "${NATIVE_STAGE_DIR}/libStorageDirect.so"
cp -- "${LICENSE_SOURCE}" "${LICENSE_STAGE}"
STAGED_COMPLIANCE_FILES+=("${LICENSE_STAGE}")
cp -- "${NOTICE_SOURCE}" "${NOTICE_STAGE}"
STAGED_COMPLIANCE_FILES+=("${NOTICE_STAGE}")

sed -i \
  "/^\[package\]/,/^\[/ s/^version = \".*\"/version = \"${RUST_VERSION}\"/" \
  "${MANIFEST_FILE}"
sed -i \
  "/^name = \"dacs-rust-client\"$/,/^$/ s/^version = \".*\"/version = \"${RUST_VERSION}\"/" \
  "${LOCK_FILE}"

rm -f -- "${TARGET_DIR}"/package/*.crate

echo "Packaging Rust client ${RUST_VERSION} for ${BUILD_ARCH}..."
cargo package \
  --allow-dirty \
  --locked \
  --manifest-path "${CRATE_DIR}/Cargo.toml" \
  --no-verify

CRATE_FILE="$(find "${TARGET_DIR}/package" -maxdepth 1 -type f -name '*.crate' -print -quit)"
if [[ -z "${CRATE_FILE}" ]]; then
  echo "Cargo did not produce a crate under ${TARGET_DIR}/package" >&2
  exit 1
fi

if ! tar -tzf "${CRATE_FILE}" |
  grep -E "^[^/]+/native/${NATIVE_DIR}/libStorageDirect\\.so$" >/dev/null; then
  echo "Crate is missing native/${NATIVE_DIR}/libStorageDirect.so: ${CRATE_FILE}" >&2
  exit 1
fi

for compliance_file in LICENSE NOTICE.txt; do
  if ! tar -tzf "${CRATE_FILE}" |
    grep -E "^[^/]+/${compliance_file}$" >/dev/null; then
    echo "Crate is missing ${compliance_file}: ${CRATE_FILE}" >&2
    exit 1
  fi
done

echo "Rust client crate ready: ${CRATE_FILE}"
