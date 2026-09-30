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
readonly VERIFY_SCRIPT="${SCRIPT_DIR}/../publish/verify-release-images.sh"
TEST_DIR=""

cleanup() {
    if [[ -n "${TEST_DIR}" ]]; then
        rm -rf -- "${TEST_DIR}"
    fi
}
trap cleanup EXIT

fail() {
    echo "$1" >&2
    exit 1
}

assert_contains() {
    local content="$1"
    local expected="$2"

    grep -F -- "${expected}" <<<"${content}" >/dev/null ||
        fail "Expected output containing: ${expected}"
}

TEST_DIR="$(mktemp -d)"
mkdir -p "${TEST_DIR}/bin"
cat >"${TEST_DIR}/bin/docker" <<'EOF'
#!/bin/bash
set -euo pipefail
printf '%s\n' "$*" >>"${TEST_DOCKER_LOG:?}"
if [[ -n "${TEST_MISSING_IMAGE:-}" && "$*" == *"/${TEST_MISSING_IMAGE}:"* ]]; then
    exit 1
fi
EOF
chmod +x "${TEST_DIR}/bin/docker"

PATH="${TEST_DIR}/bin:${PATH}" \
TEST_DOCKER_LOG="${TEST_DIR}/docker.log" \
DOCKER_REGISTRY="example.azurecr.io" \
BUILD_NUMBER="v1.2.3" \
    "${VERIFY_SCRIPT}"

for image in cache-server cacheserver-scheduler cache-controller; do
    grep -F -- \
        "buildx imagetools inspect example.azurecr.io/dacs/${image}:v1.2.3" \
        "${TEST_DIR}/docker.log" >/dev/null ||
        fail "Expected verification for ${image}"
done

if PATH="${TEST_DIR}/bin:${PATH}" \
    TEST_DOCKER_LOG="${TEST_DIR}/docker.log" \
    TEST_MISSING_IMAGE="cache-controller" \
    DOCKER_REGISTRY="example.azurecr.io" \
    BUILD_NUMBER="v1.2.3" \
        "${VERIFY_SCRIPT}" >/dev/null 2>&1; then
    fail "Expected missing chart image verification to fail"
fi

publish_output="$(
    make -C "${REPOSITORY_ROOT}" --no-print-directory -n publish-images \
        DOCKER_REGISTRY=example.azurecr.io \
        BUILD_NUMBER=v1.2.3 \
        BUILD_ARCH=amd64
)"
assert_contains \
    "${publish_output}" \
    'DOCKER_REGISTRY="example.azurecr.io/dacs"'
assert_contains \
    "${publish_output}" \
    'BUILD_NUMBER="v1.2.3"'

expected_container_version="$("${REPOSITORY_ROOT}/.pipelines/scripts/get-version.sh" container)"
default_publish_output="$(
    make -C "${REPOSITORY_ROOT}" --no-print-directory -n publish-images \
        DOCKER_REGISTRY=example.azurecr.io \
        BUILD_ARCH=amd64
)"
assert_contains \
    "${default_publish_output}" \
    "BUILD_NUMBER=\"${expected_container_version}\""

dacs_deploy_output="$(
    make -C "${REPOSITORY_ROOT}" --no-print-directory -n helm-dacs-install \
        CLUSTER_TYPE=aks \
        DOCKER_REGISTRY=example.azurecr.io \
        BUILD_NUMBER=v1.2.3 \
        DACS_CACHE_CHART_VERSION=1.2.3 \
        HELM_BIN=/bin/true
)"
assert_contains \
    "${dacs_deploy_output}" \
    'oci://example.azurecr.io/dacs/helmchart/dacs'
assert_contains \
    "${dacs_deploy_output}" \
    '--set global.registry=example.azurecr.io'

expected_chart_version="$("${REPOSITORY_ROOT}/.pipelines/scripts/get-version.sh" helm)"
default_dacs_deploy_output="$(
    make -C "${REPOSITORY_ROOT}" --no-print-directory -n helm-dacs-install \
        CLUSTER_TYPE=aks \
        DOCKER_REGISTRY=example.azurecr.io \
        HELM_BIN=/bin/true
)"
assert_contains \
    "${default_dacs_deploy_output}" \
    "--version ${expected_chart_version}"
assert_contains \
    "${default_dacs_deploy_output}" \
    "--set manager.image.tag=${expected_container_version}"

cache_deploy_output="$(
    make -C "${REPOSITORY_ROOT}" --no-print-directory -n helm-install \
        CLUSTER_TYPE=aks \
        DOCKER_REGISTRY=example.azurecr.io \
        BUILD_NUMBER=v1.2.3 \
        HELM_CHART_VERSION=1.2.3 \
        HELM_BIN=/bin/true
)"
assert_contains \
    "${cache_deploy_output}" \
    'oci://example.azurecr.io/dacs/helmchart/cache-server'
assert_contains \
    "${cache_deploy_output}" \
    '--set cacheServer.image.repository=example.azurecr.io/dacs/cache-server'
