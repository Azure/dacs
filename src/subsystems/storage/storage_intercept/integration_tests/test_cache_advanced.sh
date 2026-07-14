#!/bin/bash
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

#===============================================================================
# CACHE ADVANCED TESTS
#===============================================================================
# This script runs comprehensive cache server tests with snapshots and eviction.
# Runs in ~25 minutes (longest test suite). Single cache server lifecycle.
#
# Tests included:
# - Cache server with snapshot generation (1-second intervals)
# - Cache server with eviction enabled
# - Unit tests with cache enabled
# - Integration tests with cache
# - Cache discovery tests
# - Encryption tests
#===============================================================================

set -e

# Source common utilities
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
export BUILD_CONFIGURATION="${1:-Debug}"
source "${SCRIPT_DIR}/test_common.sh"

echo "================================================"
echo "CACHE ADVANCED TESTS - Starting"
echo "================================================"

#===============================================================================
# SECTION 9: CACHE-ENABLED TESTS (SNAPSHOT ENABLED, COMPREHENSIVE TESTING)
#===============================================================================

# Start cache server with continuous snapshotting every second and eviction enabled
export CACHESERVER_HOSTNAME="testhost"
export CACHESERVER_SNAPSHOT_INTERVAL="1"
export CACHESERVER_EVICTION_ENABLED="true"
export CACHESERVER_EVICTION_PASS_FREQUENCY="30s"
start_cache_server /tmp/cacheserver_advanced.log
unset CACHESERVER_HOSTNAME
unset CACHESERVER_SNAPSHOT_INTERVAL
unset CACHESERVER_EVICTION_ENABLED
unset CACHESERVER_EVICTION_PASS_FREQUENCY
echo "Sleeping 10 secs to allow cache server to start and initialize"
sleep 10

# Check if cache server is still running
if ! kill -0 ${CACHE_SERVER_PID} 2>/dev/null; then
  echo "ERROR: CacheServer (pid: ${CACHE_SERVER_PID}) died during startup. Logs:"
  cat /tmp/cacheserver_advanced.log || true
  exit 1
fi

# Test liveness of the cache server
test_liveness

# Run the cache server test
run_cacheserver_test

echo "Running unit tests against blob with cache enabled"
add_interception_config_blob_with_cache /mnt/dataPathIntcpt
cd ${REPOSITORY_ROOT}/build/$BUILD_CONFIGURATION/$BUILD_ARCH
eval "ctest -V --no-tests=error -j -1 -R StorageIntercept"
if [ $? -ne 0 ]
then
echo "Unit tests against blob with cache failed"
exit 1
fi
echo -e "Unit tests against blob with cache passed\n\n"

echo "Running integration test against blob with cache"
add_interception_config_blob_with_cache /mnt/quiverfs
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d large -s -n 20 -p 4"
if [ $? -ne 0 ]
then
echo "Blob Integration test with cache failed"
exit 1
fi
echo -e "Blob Integration test with cache passed\n\n"

echo "Running integration test against blob with discovery cache enabled"
add_interception_config_blob_with_cache /mnt/quiverfs false true
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d large -s -n 20 -p 4 -e"
if [ $? -ne 0 ]
then
echo "Blob Integration test with discovery cache enabled failed"
exit 1
fi
echo -e "Blob Integration test with discovery cache enabled passed\n\n"

echo "Running unit tests against blob with cache and encryption enabled"
add_interception_config_blob_with_cache_encryption /mnt/dataPathIntcpt
cd ${REPOSITORY_ROOT}/build/$BUILD_CONFIGURATION/$BUILD_ARCH
eval "ctest -V --no-tests=error -j -1 -R StorageIntercept"
if [ $? -ne 0 ]
then
echo "Unit tests against blob with cache and encryption failed"
exit 1
fi
echo -e "Running unit tests against blob with cache and encryption passed\n\n"

echo "Running integration test against blob with cache and encryption"
add_interception_config_blob_with_cache_encryption /mnt/quiverfs true
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d largeMultiMountDynamic -s -n 20 -p 4"
if [ $? -ne 0 ]
then
echo "Blob Integration test with cache and encryption failed"
exit 1
fi
echo -e "Running integration test against blob with cache and encryption passed\n\n"

stop_cache_server

echo "================================================"
echo "CACHE ADVANCED TESTS - COMPLETED SUCCESSFULLY"
echo "================================================"
