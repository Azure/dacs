#!/bin/bash

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
# - Checksum end-to-end tests
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

source "${SCRIPT_DIR}/checksum_e2e_test.sh"
__run_checksum_e2e_test__ xxhash32

echo "================================================"
echo "CACHE SPECIALIZED TESTS - COMPLETED SUCCESSFULLY"
echo "================================================"
