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
# CACHE BASIC INTEGRATION TESTS
#===============================================================================
# This script runs basic cache server integration tests without snapshots.
# Runs in ~20 minutes. Single cache server lifecycle.
#
# Tests included:
# - Cache server with default configuration (no snapshots)
# - Performance client tests (upload/download)
# - Cache eviction client tests
# - Last access distribution client tests
# - Cache client library tests
# - Rust cache client integration tests
# - Local SSD cache tests
#===============================================================================

set -e

# Source common utilities
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
export BUILD_CONFIGURATION="${1:-Debug}"
source "${SCRIPT_DIR}/test_common.sh"

echo "================================================"
echo "CACHE BASIC INTEGRATION TESTS - Starting"
echo "================================================"

#===============================================================================
# SECTION 7: CACHE-ENABLED TESTS (SNAPSHOT DISABLED)
#===============================================================================

start_cache_server /dev/null
sleep 10

# Ensure perf client is wired up correctly
PERF_CLIENT_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/cache-test/PerfClient "
random_prefix=$(date +%s)
eval "${PERF_CLIENT_EXE} --server-hostname 127.0.0.1 --server-port ${CACHE_SERVER_PORT} --num-threads 16 --data-size-GiB 1 --prefix ${random_prefix} --mode upload"
if [ $? -ne 0 ]
then
    echo "Perf client upload test failed"
    exit 1
fi

eval "${PERF_CLIENT_EXE} --server-hostname 127.0.0.1 --server-port ${CACHE_SERVER_PORT} --num-threads 16 --data-size-GiB 1 --prefix ${random_prefix} --mode download"
if [ $? -ne 0 ]
then
    echo "Perf client download test failed"
    exit 1
fi

# Ensure eviction client is wired up correctly
EVICTION_CLIENT_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/cache-test/PerfClient "
eval "${EVICTION_CLIENT_EXE} --eviction-client --server-hostname 127.0.0.1 --server-port ${CACHE_SERVER_PORT} --last-access-time "2024-04-02T20:00:00.000Z" | tee /tmp/eviction.log"
if [ $? -ne 0 ]
then
    echo "Eviction client test failed"
    exit 1
fi

if ! grep -q "Successfully requested eviction" /tmp/eviction.log; then
    echo "Eviction client test failed"
    exit 1
fi

# Ensure last access distribution client is wired up correctly
LAST_ACCESS_DISTRIBUTION_CLIENT_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/cache-test/PerfClient "
eval "${LAST_ACCESS_DISTRIBUTION_CLIENT_EXE} --last-access-time-distribution-client --server-hostname 127.0.0.1 --server-port ${CACHE_SERVER_PORT}  --distribution-bucket-size-in-hours 1 | tee /tmp/lastAccessDistribution.log"

if [ $? -ne 0 ]
then
    echo "Last access distribution client test failed"
    exit 1
fi

if ! grep -q "Successfully requested get last access distribution" /tmp/lastAccessDistribution.log; then
    echo "Last access distribution client test failed"
    exit 1
fi

echo "Running cache client lib test"
cd ${REPOSITORY_ROOT}/build/$BUILD_CONFIGURATION/$BUILD_ARCH
eval "ctest -V --no-tests=error -j -1 -R CacheClientLibTests"
if [ $? -ne 0 ]
then
echo "cache client lib unit test failed"
exit 1
fi
echo -e "cache client lib unit test passed\n\n"

echo "Running rust cache client integration test"

pushd ${REPOSITORY_ROOT}/src/subsystems/storage/cache_client/rust_tests
export LD_LIBRARY_PATH="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage-cache-client:${LD_LIBRARY_PATH}"
export LIBRARY_PATH="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage-cache-client"
export CACHE_CLIENT_LIB_DIR="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage-cache-client"
export CACHE_CLIENT_HEADER_FILE="${REPOSITORY_ROOT}/src/subsystems/storage/cache_client/CacheClient.h"

eval "cargo run"
if [ $? -ne 0 ];
then
echo "Rust cache client integration test failed"
exit 1
fi
popd
echo -e "cache client lib integration test passed\n\n"

echo "Running sub-chunk read integration test"
add_interception_config_blob_with_cache /mnt/quiverfs
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d small -s -n 5 -p 1"
if [ $? -ne 0 ]
then
echo "Sub-chunk read integration test failed"
exit 1
fi
echo -e "Sub-chunk read integration test passed\n\n"

stop_cache_server

#===============================================================================
# SECTION: LOCAL SSD CACHE TESTS
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

echo "Running unit tests against blob with local ssd cache"
reset_local_ssd_cache
add_interception_config_blob_with_localssdcache /mnt/dataPathIntcpt
cd ${REPOSITORY_ROOT}/build/$BUILD_CONFIGURATION/$BUILD_ARCH
eval "ctest -V --no-tests=error -j -1 -R StorageIntercept"
if [ $? -ne 0 ]
then
echo "Unit tests against blob with local ssd cache failed"
exit 1
fi
echo -e "Unit tests against blob with local ssd cache passed\n\n"

echo "Running integration test against blob with local ssd cache"
reset_local_ssd_cache
add_interception_config_blob_with_localssdcache /mnt/quiverfs
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d large -s -n 20 -p 4"
if [ $? -ne 0 ]
then
echo "Blob Integration test with local ssd cache failed"
exit 1
fi
echo -e "Blob Integration test with local ssd cache passed\n\n"

reset_local_ssd_cache
stop_cache_server

echo "================================================"
echo "CACHE BASIC INTEGRATION TESTS - Completed Successfully"
echo "================================================"
