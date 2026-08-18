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

# End-to-end checksum validation on both legs of the read path:
#   Leg 1 (blob -> cache): Azure CRC64 on SDK-client downloads (remoteBlobUseSdkClient +
#          remoteBlobChecksumEnabled).
#   Leg 2 (cache -> client): xxHash (cacheCalculate/ValidateChecksumEnabled, xxhash32).
# Sourced by test_cache_specialized.sh via __run_checksum_both_legs_test__; also runs
# standalone (auto-sources test_common.sh). Auth: STORAGE_INTCPT_SAS_TOKEN or SI_azBlobAccessToken.

CKSUM_SCRIPT_DIR="$( cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 ; pwd -P )"

if ! declare -f add_interception_config_blob_with_remote_cache >/dev/null 2>&1; then
    export BUILD_CONFIGURATION="${BUILD_CONFIGURATION:-Debug}"
    export BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
    source "${CKSUM_SCRIPT_DIR}/test_common.sh"
fi

CKSUM_BIN="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage"
CKSUM_MOUNT="/mnt/quiverfs"
CKSUM_TESTCASES="${CKSUM_SCRIPT_DIR}/large_dataset_testcases.txt"

__run_checksum_both_legs_test__() {
    echo "=================================================="
    echo "Checksum both-legs validation: Leg1 CRC64 (blob->cache) + Leg2 xxHash (cache->client)"
    echo "=================================================="

    local lib="${CKSUM_BIN}/libStorageIntercept.so"
    local exe="${CKSUM_BIN}/StorageInterceptIntegTest"
    if [[ ! -f "${lib}" || ! -f "${exe}" ]]; then
        echo "ERROR: required binaries not found under ${CKSUM_BIN}"; return 1
    fi

    mkdir -p "${CKSUM_MOUNT}"

    # Fresh CacheServer so Leg 1 is a genuine cold (blob) read.
    stop_cache_server
    rm -rf "${CACHE_SERVER_PATH}"; mkdir -p "${CACHE_SERVER_PATH}"
    start_cache_server /tmp/checksum_both_legs_cache_server.log
    sleep 5
    test_liveness

    add_interception_config_blob_with_remote_cache "${CKSUM_MOUNT}"
    {
        echo "remoteBlobUseSdkClient true"
        echo "remoteBlobChecksumEnabled true"
        echo "cacheCalculateChecksumEnabled true"
        echo "cacheValidateChecksumEnabled true"
        echo "cacheChecksumAlgorithm xxhash32"
    } >> "${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"
    # Local/SAS runs: add SAS token as an env reference so the literal never lands on disk.
    if [ -z "${SI_azBlobAccessToken:-}" ] && [ -n "${STORAGE_INTCPT_SAS_TOKEN:-}" ]; then
        echo 'azBlobSASToken ${STORAGE_INTCPT_SAS_TOKEN}' >> "${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"
    fi
    echo "Config =>"; cat "${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"

    export STORAGE_INTEG_TESTCASE_FILE="${CKSUM_TESTCASES}"

    echo "--- Leg 1: cold read (Blob -> DACS, CRC64 validated; populate cache) ---"
    if ! LD_PRELOAD="${lib}" "${exe}" "${CKSUM_MOUNT}" 1 1; then
        echo "FAILED: Leg 1 (cold read / CRC64)"; stop_cache_server; return 1
    fi

    echo "--- Leg 2: warm read (CacheServer -> Client, xxHash validated) ---"
    local leg2_log="/tmp/checksum_both_legs_leg2.log"
    LD_PRELOAD="${lib}" "${exe}" "${CKSUM_MOUNT}" 1 1 2>&1 | tee "${leg2_log}"
    local leg2_rc=${PIPESTATUS[0]}
    if [ ${leg2_rc} -ne 0 ]; then
        echo "FAILED: Leg 2 (warm read / xxHash) rc=${leg2_rc}"; stop_cache_server; return 1
    fi

    # Guard: warm leg MUST be served from the CacheServer (RemoteCache>0), else reads fell back
    # to blob and Leg 2 xxHash was never exercised.
    local remote_cache_reads
    remote_cache_reads=$(grep -oE 'RemoteCache=[0-9]+' "${leg2_log}" | grep -oE '[0-9]+' | sort -nr | head -1)
    if [ -z "${remote_cache_reads}" ] || [ "${remote_cache_reads}" -eq 0 ]; then
        echo "FAILED: Leg 2 not served from CacheServer (RemoteCache=${remote_cache_reads:-0}); xxHash not exercised."
        stop_cache_server; return 1
    fi
    echo "Leg 2 served ${remote_cache_reads} chunk(s) from the CacheServer (xxHash validated)"

    stop_cache_server
    echo "PASSED: checksum both-legs validation (Leg1 CRC64 + Leg2 xxHash, data intact)"
    return 0
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    __run_checksum_both_legs_test__
    exit $?
fi
