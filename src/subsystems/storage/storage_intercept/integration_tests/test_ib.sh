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

# Increase fd limit for splice() zero-copy path which uses pipe fds per upload
ulimit -n 65536 2>/dev/null || ulimit -n 8192 2>/dev/null || echo "Warning: could not increase fd limit (current: $(ulimit -n))"
echo "File descriptor limit: $(ulimit -n)"

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
export TEST_IB_ONLY=true
run_cacheserver_test

# Stop cache server
stop_cache_server

# Run PerfClient with InfiniBand enabled
PERF_CLIENT_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/cache-test/PerfClient"
random_prefix="ib_perf_$(date +%s%N)_"

# Validates that IB was used and no fallback to ethernet occurred
# Usage: verify_ib_used <log_file> <label>
verify_ib_used() {
    local log_file="$1"
    local label="$2"
    echo "Verifying InfiniBand was used by PerfClient (${label})..."
    if ! grep -q "IB Init successful" "${log_file}"; then
        echo "ERROR: [${label}] 'IB Init successful' not found - IB device initialization failed"
        cat "${log_file}"
        exit 1
    fi
    if grep -q "falling back to socket connection" "${log_file}"; then
        echo "ERROR: [${label}] PerfClient fell back to socket (ethernet) connection - IB was not used"
        cat "${log_file}"
        exit 1
    fi
    if grep -q "server does not support IB connections" "${log_file}"; then
        echo "ERROR: [${label}] Cache server rejected IB setup - IB was not used"
        cat "${log_file}"
        exit 1
    fi
    echo "Verified: InfiniBand was successfully used by PerfClient (${label})"
    rm -f "${log_file}"
}

start_cache_server /dev/null
echo "Sleeping 10 secs to allow cache server to start and initialize"
sleep 10

# Ensure PerfClient failures aren't masked by a successful tee in the pipeline.
set -o pipefail

PERF_CLIENT_UPLOAD_LOG="/tmp/perf_client_ib_upload_$$.log"
eval "${PERF_CLIENT_EXE} --server-hostname 127.0.0.1 --server-port ${CACHE_SERVER_PORT} --num-threads 16 --data-size-GiB 2 --prefix ${random_prefix} --mode upload --ib-enabled true" 2>&1 | tee "${PERF_CLIENT_UPLOAD_LOG}"
verify_ib_used "${PERF_CLIENT_UPLOAD_LOG}" "upload"

PERF_CLIENT_DOWNLOAD_LOG="/tmp/perf_client_ib_download_$$.log"
eval "${PERF_CLIENT_EXE} --server-hostname 127.0.0.1 --server-port ${CACHE_SERVER_PORT} --num-threads 16 --data-size-GiB 2 --prefix ${random_prefix} --mode download --ib-enabled true" 2>&1 | tee "${PERF_CLIENT_DOWNLOAD_LOG}"
verify_ib_used "${PERF_CLIENT_DOWNLOAD_LOG}" "download"

stop_cache_server

echo "================================================"
echo "INFINIBAND CACHE TESTS - Completed Successfully"
echo "================================================"
