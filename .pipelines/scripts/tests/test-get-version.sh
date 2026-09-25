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
readonly VERSION_HELPER="${SCRIPT_DIR}/../get-version.sh"
TEST_REPOSITORY=""

cleanup() {
    if [[ -n "${TEST_REPOSITORY}" ]]; then
        rm -rf -- "${TEST_REPOSITORY}"
    fi
}
trap cleanup EXIT

assert_version() {
    local expected="$1"
    local format="$2"
    local actual

    actual="$(DACS_REPOSITORY_ROOT="${TEST_REPOSITORY}" "${VERSION_HELPER}" "${format}")"
    if [[ "${actual}" != "${expected}" ]]; then
        echo "Expected ${format} version '${expected}', got '${actual}'." >&2
        return 1
    fi
}

assert_universal_conversion() {
    local expected="$1"
    local source_version="$2"
    local actual

    actual="$(bash -c '
        helper="$1"
        source_version="$2"
        shift 2
        source "${helper}"
        to_universal_version "${source_version}"
    ' _ "${VERSION_HELPER}" "${source_version}")"
    if [[ "${actual}" != "${expected}" ]]; then
        echo "Expected universal version '${expected}', got '${actual}'." >&2
        return 1
    fi
}

TEST_REPOSITORY="$(mktemp -d)"
git -C "${TEST_REPOSITORY}" init --quiet
git -C "${TEST_REPOSITORY}" config user.email "test@example.com"
git -C "${TEST_REPOSITORY}" config user.name "DACS Version Test"
touch "${TEST_REPOSITORY}/first"
git -C "${TEST_REPOSITORY}" add first
git -C "${TEST_REPOSITORY}" commit --quiet -m "first"
touch "${TEST_REPOSITORY}/second"
git -C "${TEST_REPOSITORY}" add second
git -C "${TEST_REPOSITORY}" commit --quiet -m "second"

sha="$(git -C "${TEST_REPOSITORY}" rev-parse --short=14 HEAD)"
readonly sha
assert_version "v0.1.0-alpha.0.2+${sha}" source
assert_version "v0.1.0-alpha.0.2_${sha}" container
assert_version "0.1.0-alpha.0.2+${sha}" helm
assert_version "0.1.0-alpha.0.2.g${sha}" rust
assert_version "0.1.0a0.dev2" python
assert_version "0.1.0-alpha.0.2.g${sha}" universal
assert_universal_conversion \
    "0.1.0-alpha.0.2.g01234567890123" \
    "v0.1.0-alpha.0.2+01234567890123"

git -C "${TEST_REPOSITORY}" tag v0.1.0
assert_version "v0.1.0" source
assert_version "0.1.0" python
assert_version "0.1.0" rust
assert_version "0.1.0" universal

git -C "${TEST_REPOSITORY}" tag --delete v0.1.0 >/dev/null
git -C "${TEST_REPOSITORY}" tag v0.2.0-rc.1
assert_version "v0.2.0-rc.1" source
assert_version "0.2.0rc1" python
assert_version "0.2.0-rc.1" rust
assert_version "0.2.0-rc.1" universal

touch "${TEST_REPOSITORY}/third"
git -C "${TEST_REPOSITORY}" add third
git -C "${TEST_REPOSITORY}" commit --quiet -m "third"
dev_sha="$(git -C "${TEST_REPOSITORY}" rev-parse --short=14 HEAD)"
readonly dev_sha
assert_version "v0.2.0-rc.1.1+${dev_sha}" source
assert_version "0.2.0rc1.dev1" python
assert_version "0.2.0-rc.1.1.g${dev_sha}" rust
assert_version "0.2.0-rc.1.1.g${dev_sha}" universal
