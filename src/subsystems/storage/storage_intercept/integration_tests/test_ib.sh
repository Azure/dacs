#!/bin/bash

#===============================================================================
# INFINIBAND CACHE TESTS
#===============================================================================
# This script runs cache server tests with InfiniBand RDMA enabled.
# Runs in Docker container with IB device passthrough.
#
# Tests included:
# - All standard cache server tests
# - InfiniBand-specific RDMA tests (TestIBAPI, TestIBAPIParallel)
#===============================================================================

set -e

# Source common utilities
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
export BUILD_CONFIGURATION="${1:-Release}"
source "${SCRIPT_DIR}/test_common.sh"

echo "================================================"
echo "INFINIBAND CACHE TESTS - Starting"
echo "================================================"

# Verify InfiniBand hardware is available
if [ ! -d /sys/class/infiniband ]; then
    echo "ERROR: No InfiniBand devices found in /sys/class/infiniband"
    echo "This test requires InfiniBand-enabled hardware"
    exit 1
fi

echo "InfiniBand devices detected:"
ls -la /sys/class/infiniband/ || true
ls -la /dev/infiniband/ || true

#===============================================================================
# INFINIBAND CACHE SERVER TESTS
#===============================================================================

# Start cache server with InfiniBand support
# (IB is enabled by default in CacheServer command)
start_cache_server /dev/null
echo "Sleeping 10 secs to allow cache server to start and initialize"
sleep 10

# Test liveness of the cache server
test_liveness

# Run cache server tests with InfiniBand enabled
export TEST_IB=true
run_cacheserver_test

# Stop cache server
stop_cache_server

echo "================================================"
echo "INFINIBAND CACHE TESTS - Completed Successfully"
echo "================================================"
