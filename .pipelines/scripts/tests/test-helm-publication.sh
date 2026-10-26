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
readonly TEST_CHART_VERSION="0.1.0-alpha.0.1+abcdef12345678"
readonly TEST_APP_VERSION="v0.1.0-alpha.0.1+abcdef12345678"
readonly SAFE_TEST_APP_VERSION="v0.1.0-alpha.0.1_abcdef12345678"

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf -- "${TEMP_DIR}"' EXIT

assert_safe_labels() {
    local chart_dir="$1"
    local chart_name="$2"
    local chart_package="${TEMP_DIR}/${chart_name}-${TEST_CHART_VERSION}.tgz"
    local rendered

    helm dependency build "${chart_dir}" --skip-refresh >/dev/null
    helm package "${chart_dir}" \
        --destination "${TEMP_DIR}" \
        --version "${TEST_CHART_VERSION}" \
        --app-version "${TEST_APP_VERSION}" >/dev/null

    rendered="$(helm template test-release "${chart_package}")"

    grep -Fq "app.kubernetes.io/version: \"${SAFE_TEST_APP_VERSION}\"" <<<"${rendered}"
    if grep -E '^[[:space:]]*(app.kubernetes.io/version|helm.sh/chart):.*\+' <<<"${rendered}"; then
        echo "${chart_name} rendered a Kubernetes label containing '+'" >&2
        return 1
    fi
}

assert_safe_labels "${REPOSITORY_ROOT}/helm/dacs-prereq" "dacs-prereq"
assert_safe_labels "${REPOSITORY_ROOT}/src/subsystems/cache-controller/charts/dacs" "dacs"

OUTPUT_DIR="${TEMP_DIR}/dist"
METADATA_FILE="${TEMP_DIR}/helm-chart.txt"
DOCKER_REGISTRY="example.azurecr.io" \
DACS_HELM_OUTPUT_DIR="${OUTPUT_DIR}" \
DACS_HELM_METADATA_FILE="${METADATA_FILE}" \
    "${REPOSITORY_ROOT}/.pipelines/scripts/publish/publish-helm-chart.sh"

container_version="$("${REPOSITORY_ROOT}/.pipelines/scripts/get-version.sh" container)"
chart_version="$("${REPOSITORY_ROOT}/.pipelines/scripts/get-version.sh" helm)"
dacs_package="${OUTPUT_DIR}/dacs-${chart_version}.tgz"

helm show chart "${dacs_package}" |
    grep -Fq "appVersion: ${container_version}"

rendered_dacs="$(helm template test-release "${dacs_package}")"
grep -Fq "example.azurecr.io/dacs/cache-controller:${container_version}" <<<"${rendered_dacs}"
grep -Fq "example.azurecr.io/dacs/cache-server:${container_version}" <<<"${rendered_dacs}"
rendered_prereq="$(helm template test-release "${OUTPUT_DIR}/dacs-prereq-${chart_version}.tgz")"
grep -Fq "example.azurecr.io/dacs/cache-setup:${container_version}" <<<"${rendered_prereq}"

for chart in cache-server dacs; do
    rendered_parent="$(helm template test-release "${OUTPUT_DIR}/${chart}-${chart_version}.tgz" \
        --set cacheServerPrereq.enabled=true)"
    grep -Fq "example.azurecr.io/dacs/cache-setup:${container_version}" <<<"${rendered_parent}"
    grep -Fq "helm.sh/hook: post-install,post-upgrade" <<<"${rendered_parent}"
done

grep -Fq "CACHE_CONTROLLER_CHART_VERSION=${chart_version}" "${METADATA_FILE}"
grep -Fq "APP_VERSION=$("${REPOSITORY_ROOT}/.pipelines/scripts/get-version.sh" source)" "${METADATA_FILE}"

echo "Helm publication tests passed"
