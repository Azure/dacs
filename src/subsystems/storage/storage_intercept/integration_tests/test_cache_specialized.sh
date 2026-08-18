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

#===============================================================================
# CACHE SPECIALIZED TESTS
#===============================================================================
# This script runs specialized validation tests for advanced cache features.
# Runs in ~15 minutes. Multiple cache server instances (start/stop per test).
#
# Tests included:
# - Snapshot validation and restart scenarios
# - Cache eviction validation and parallel operations
# - Last access time distribution analysis
# - Remote cache tests
#===============================================================================

set -e

# Source common utilities
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
export BUILD_CONFIGURATION="${1:-Debug}"
source "${SCRIPT_DIR}/test_common.sh"

echo "================================================"
echo "CACHE SPECIALIZED TESTS - Starting"
echo "================================================"

#===============================================================================
# SECTION 10: SPECIALIZED VALIDATION AND INTEGRATION TESTS
#===============================================================================

echo "Running snapshot validation tests"
source "$SCRIPT_DIR/snapshot_integration_test.sh"
__run_restart_snapshot_basic_test__
__test_snapshot_disk_mismatch__
__test_no_crash_on_invalid_snapshot__
__test_upload_download_snapshot_in_parallel__
__test_no_crash_on_truncated_snapshot__
echo -e "Snapshot validation tests passed\n\n"

echo "Running cache eviction validation tests"
source "$SCRIPT_DIR/eviction_integration_test.sh"
__run_cache_client_lib_test_with_eviction__
__run_eviction_test_with_parallel_upload_download__
__run_eviction_test_with_restart_snapshot__
__run_delete_by_last_access_time__
echo -e "Cache eviction validation tests passed\n\n"

source "$SCRIPT_DIR/last_access_distribution_integration_test.sh"
__run__test__last_access_time_distribution__

#===============================================================================
# SECTION: REMOTE CACHE TESTS
#===============================================================================

export CACHESERVER_HOSTNAME="testhost"
export CACHESERVER_SNAPSHOT_INTERVAL="1"
export CACHESERVER_EVICTION_ENABLED="true"
export CACHESERVER_EVICTION_PASS_FREQUENCY="30s"
start_cache_server /dev/null
unset CACHESERVER_HOSTNAME
unset CACHESERVER_SNAPSHOT_INTERVAL
unset CACHESERVER_EVICTION_ENABLED
unset CACHESERVER_EVICTION_PASS_FREQUENCY
echo "Sleeping 10 secs to allow cache server to start and initialize"
sleep 10
test_liveness

echo "Running unit tests against blob with remote attribute and data cache enabled"
add_interception_config_blob_with_remote_cache /mnt/dataPathIntcpt
cd ${REPOSITORY_ROOT}/build/$BUILD_CONFIGURATION/$BUILD_ARCH
ctest -V --no-tests=error -j -1 -R StorageIntercept
if [ $? -ne 0 ]
then
echo "Unit tests against blob with remote attribute and data cache failed"
exit 1
fi
echo -e "Unit tests against blob with remote attribute and data cache passed\n\n"

echo "Running integration test against blob with remote attribute and data cache enabled"
add_interception_config_blob_with_remote_cache /mnt/quiverfs false true
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d large -s -n 20 -p 4 -f"
if [ $? -ne 0 ]
then
echo "Blob Integration test with remote attribute and data cache failed"
exit 1
fi
echo -e "Blob Integration test with remote attribute and data cache passed\n\n"

echo "Running integration test with multi-mount against blob with remote attribute and data cache enabled"
add_interception_config_blob_with_remote_cache /mnt/quiverfs true
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d largeMultiMountDynamic -s -n 20 -p 4"
if [ $? -ne 0 ]
then
echo "Blob Integration test with multi-mount and remote attribute and data cache failed"
exit 1
fi
echo -e "Blob Integration test with multi-mount and remote attribute and data cache passed\n\n"

# Checksum both-legs validation: Leg 1 CRC64 (blob->cache via SdkBlobClient) + Leg 2 xxHash (cache->client).
echo "Running checksum both-legs test (Leg1 CRC64 + Leg2 xxHash)"
source "$SCRIPT_DIR/checksum_both_legs_test.sh"
if ! __run_checksum_both_legs_test__; then
    echo "checksum both-legs test failed"
    exit 1
fi
echo -e "checksum both-legs test passed\n\n"

stop_cache_server

echo "================================================"
echo "CACHE SPECIALIZED TESTS - COMPLETED SUCCESSFULLY"
echo "================================================"
