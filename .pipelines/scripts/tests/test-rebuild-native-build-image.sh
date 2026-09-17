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
readonly SCRIPT="${SCRIPT_DIR}/../build/rebuild-native-build-image.sh"
readonly TEST_SHA="0123456789abcdef0123456789abcdef01234567"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/../build/build-cache.sh"
TEST_DIR=""

cleanup() {
    if [[ -n "$TEST_DIR" ]]; then
        rm -rf -- "$TEST_DIR"
    fi
}
trap cleanup EXIT

fail() {
    echo "$1" >&2
    exit 1
}

assert_log_contains() {
    local expected="$1"
    grep -F -- "$expected" "$TEST_DIR/docker.log" >/dev/null ||
        fail "Expected Docker invocation containing: $expected"
}

create_test_commands() {
    TEST_DIR="$(mktemp -d)"
    mkdir -p "$TEST_DIR/bin"

    cat >"$TEST_DIR/bin/git" <<EOF
#!/bin/bash
printf '%s\n' "$TEST_SHA"
EOF

    cat >"$TEST_DIR/bin/docker" <<'EOF'
#!/bin/bash
set -euo pipefail
printf '%s\n' "$*" >>"${TEST_DOCKER_LOG:?}"
if [[ "$1" == "pull" ]]; then
    exit "${TEST_DOCKER_PULL_RESULT:-1}"
fi
EOF

    chmod +x "$TEST_DIR/bin/git" "$TEST_DIR/bin/docker"
}

create_test_commands

[[ "$(SYSTEM_PULLREQUEST_PULLREQUESTID=42 resolve_build_cache_scope pr)" == "pr-42" ]] ||
    fail "Failed to resolve a pull request cache scope from the pipeline variable."
[[ "$(BUILD_SOURCEBRANCH=refs/pull/68/merge resolve_build_cache_scope pr)" == "pr-68" ]] ||
    fail "Failed to resolve a pull request cache scope from the source branch."
if resolve_build_cache_scope "invalid/scope" >/dev/null 2>&1; then
    fail "Accepted an invalid build cache scope."
fi

PATH="$TEST_DIR/bin:$PATH" \
TEST_DOCKER_LOG="$TEST_DIR/docker.log" \
DOCKER_REGISTRY="example.azurecr.io" \
BUILD_ARCH="amd64" \
BUILD_CACHE_SCOPE="pr" \
SYSTEM_PULLREQUEST_PULLREQUESTID="68" \
    "$SCRIPT"

assert_log_contains \
    "--cache-from type=registry,ref=example.azurecr.io/dacs/native-build:buildcache-pr-68-linux-amd64"
assert_log_contains \
    "--cache-from type=registry,ref=example.azurecr.io/dacs/native-build:buildcache-main-linux-amd64"
assert_log_contains \
    "--cache-from type=registry,ref=example.azurecr.io/dacs/native-build:buildcache-linux-amd64"
assert_log_contains \
    "--cache-to type=registry,ref=example.azurecr.io/dacs/native-build:buildcache-pr-68-linux-amd64,mode=min"
assert_log_contains \
    "-t example.azurecr.io/dacs/native-build:git-${TEST_SHA}-linux-amd64"

: >"$TEST_DIR/docker.log"
PATH="$TEST_DIR/bin:$PATH" \
TEST_DOCKER_LOG="$TEST_DIR/docker.log" \
TEST_DOCKER_PULL_RESULT="0" \
DOCKER_REGISTRY="example.azurecr.io" \
BUILD_ARCH="arm64" \
BUILD_CACHE_SCOPE="main" \
    "$SCRIPT"

assert_log_contains \
    "pull example.azurecr.io/dacs/native-build:git-${TEST_SHA}-linux-arm64"
assert_log_contains \
    "tag example.azurecr.io/dacs/native-build:git-${TEST_SHA}-linux-arm64 example.azurecr.io/dacs/native-build:linux-arm64"
if grep -F -- "buildx build" "$TEST_DIR/docker.log" >/dev/null; then
    fail "BuildKit ran even though the commit-addressed image existed."
fi
