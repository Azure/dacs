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

# Helper script for devs to start a local cache server and run the CacheClientTests against it and then cleanup
# Usage: ./run_cache_client_tests.sh [Debug|Release]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" >/dev/null 2>&1; pwd -P)"
export BUILD_CONFIGURATION="${1:-Debug}"
export VALIDATE_OTEL_METRICS=true
source "${SCRIPT_DIR}/test_common.sh"

export CACHE_SERVER_PATH="${TMPDIR:-/tmp}/cache_server_$$"
CACHE_SERVER_LOG_FILE="${TMPDIR:-/tmp}/cache_server_cache_client_tests_$$.log"

cleanup() {
    local rc=$?
    trap - EXIT INT TERM
    set +e

    stop_cache_server "" >/dev/null 2>&1
    stop_otel_metrics_collector

    if [[ ${rc} -ne 0 && -f "${CACHE_SERVER_LOG_FILE}" ]]; then
        echo "CacheServer logs:"
        cat "${CACHE_SERVER_LOG_FILE}"
    fi
    rm -f "${CACHE_SERVER_LOG_FILE}"

    exit "${rc}"
}
trap cleanup EXIT INT TERM

start_otel_metrics_collector

echo "Starting CacheServer for CacheClientTests (${BUILD_CONFIGURATION})"
start_cache_server "${CACHE_SERVER_LOG_FILE}"
sleep 10
test_liveness

echo "Running CacheClientTests"
CACHE_CLIENT_TEST_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage-cache-client/CacheClientLibTests"
if [[ ! -x "${CACHE_CLIENT_TEST_EXE}" ]]; then
    echo "ERROR: CacheClientTests executable not found: ${CACHE_CLIENT_TEST_EXE}"
    exit 1
fi

CACHE_SERVER_PORT="${CACHE_SERVER_PORT}" \
CACHE_SERVER_PATH="${CACHE_SERVER_PATH}" \
    "${CACHE_CLIENT_TEST_EXE}" --gtest_filter='*CacheClientTests*'

validate_otel_metrics \
    cache_client_upload_requests \
    cache_client_download_requests

echo "CacheClientTests completed successfully"