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
readonly PUSH_SCRIPT="${SCRIPT_DIR}/../publish/push-candidate-images.sh"
readonly PROMOTE_SCRIPT="${SCRIPT_DIR}/../publish/promote-candidate-images.sh"
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

TEST_DIR="$(mktemp -d)"
mkdir -p "$TEST_DIR/bin"
cat >"$TEST_DIR/bin/docker" <<'EOF'
#!/bin/bash
set -euo pipefail
printf '%s\n' "$*" >>"${TEST_DOCKER_LOG:?}"
if [[ "$*" == *"imagetools inspect example.azurecr.io/dacs/native-build:v1.2.3-linux-"* ]]; then
    exit 1
fi
EOF
chmod +x "$TEST_DIR/bin/docker"

PATH="$TEST_DIR/bin:$PATH" \
TEST_DOCKER_LOG="$TEST_DIR/docker.log" \
DOCKER_REGISTRY="example.azurecr.io/dacs" \
BUILD_ID="123" \
BUILD_ARCH="arm64" \
    "$PUSH_SCRIPT"

assert_log_contains \
    "tag cache-server:local example.azurecr.io/dacs/cache-server:candidate-123-linux-arm64"
assert_log_contains \
    "push example.azurecr.io/dacs/cache-controller:candidate-123-linux-arm64"
[[ "$(grep -c '^push ' "$TEST_DIR/docker.log")" -eq 4 ]] ||
    fail "Expected exactly four candidate image pushes"

: >"$TEST_DIR/docker.log"
PATH="$TEST_DIR/bin:$PATH" \
TEST_DOCKER_LOG="$TEST_DIR/docker.log" \
DOCKER_REGISTRY="example.azurecr.io/dacs" \
BUILD_ID="123" \
BUILD_NUMBER="v1.2.3" \
BUILD_ARCHS="amd64 arm64" \
    "$PROMOTE_SCRIPT"

assert_log_contains \
    "buildx imagetools create --tag example.azurecr.io/dacs/cache-server:v1.2.3-linux-amd64 example.azurecr.io/dacs/cache-server:candidate-123-linux-amd64"
assert_log_contains \
    "buildx imagetools create --tag example.azurecr.io/dacs/cache-controller:v1.2.3 example.azurecr.io/dacs/cache-controller:v1.2.3-linux-amd64 example.azurecr.io/dacs/cache-controller:v1.2.3-linux-arm64"
[[ "$(grep -c '^buildx imagetools create ' "$TEST_DIR/docker.log")" -eq 12 ]] ||
    fail "Expected eight architecture promotions and four manifest promotions"

if PATH="$TEST_DIR/bin:$PATH" \
    TEST_DOCKER_LOG="$TEST_DIR/docker.log" \
    DOCKER_REGISTRY="example.azurecr.io/dacs" \
    BUILD_ID="123" \
    BUILD_ARCH="s390x" \
        "$PUSH_SCRIPT" >/dev/null 2>&1; then
    fail "Expected an unsupported candidate architecture to fail"
fi

if PATH="$TEST_DIR/bin:$PATH" \
    TEST_DOCKER_LOG="$TEST_DIR/docker.log" \
    DOCKER_REGISTRY="example.azurecr.io/dacs" \
    BUILD_ID="123" \
    BUILD_NUMBER="v1.2.3" \
    BUILD_ARCHS="amd64 s390x" \
        "$PROMOTE_SCRIPT" >/dev/null 2>&1; then
    fail "Expected an unsupported promotion architecture to fail"
fi
