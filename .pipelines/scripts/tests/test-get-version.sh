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
assert_version "0.1.0-alpha.0.2+${sha}" rust
assert_version "0.1.0a0.dev2+${sha}" python

git -C "${TEST_REPOSITORY}" tag v0.1.0
assert_version "v0.1.0" source
assert_version "0.1.0" python
