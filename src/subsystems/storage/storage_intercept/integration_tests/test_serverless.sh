#!/bin/bash

#===============================================================================
# SERVERLESS TESTS
#===============================================================================
# This script runs tests that don't require a cache server to be running.
# Runs in ~15 minutes. Fully independent - no cache server conflicts.
#
# Tests included:
# - Performance benchmarks (skippable via SKIP_PERF_BENCHMARK=true)
# - Cache server unit tests (no server needed - pure unit tests)
# - Basic blob unit tests
# - Go integration tests
# - Dynamic mount tests (dynamic container and dynamic account)
# - Log verification tests
#===============================================================================

set -e

# Source common utilities
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
export BUILD_CONFIGURATION="${1:-Debug}"
source "${SCRIPT_DIR}/test_common.sh"

echo "================================================"
echo "SERVERLESS TESTS - Starting"
echo "================================================"

#===============================================================================
# SECTION 6: UNIT TESTS AND INTEGRATION TESTS (NO CACHE SERVER REQUIRED)
#===============================================================================

__run_perf_benchmark__

echo "Running cache server unit tests"
cd ${REPOSITORY_ROOT}/build/$BUILD_CONFIGURATION/$BUILD_ARCH
eval "ctest -V --no-tests=error -j -1 -R CacheServer"
if [ $? -ne 0 ]
then
echo "Cache server unit tests failed"
exit 1
fi

__verify_nologs_unintercepted_

echo "Running unit tests against blob"
add_interception_config_blob /mnt/dataPathIntcpt
cd ${REPOSITORY_ROOT}/build/$BUILD_CONFIGURATION/$BUILD_ARCH
eval "ctest -V --no-tests=error -j -1 -R StorageIntercept"
if [ $? -ne 0 ]
then
echo "Unit tests against blob failed"
exit 1
fi
echo -e "Unit tests against blob passed\n\n"

echo "Running go integration test against blob"
if command -v go >/dev/null 2>&1; then
  add_interception_config_blob /mnt/quiverfs
  export SI_azBlobSASToken="incorrect-token"

  # Run the test
  export DACS_PATH=${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/
  pushd ${SCRIPT_DIR}
  LD_LIBRARY_PATH=$DACS_PATH CGO_CFLAGS="-I $DACS_PATH/include" CGO_LDFLAGS="-L $DACS_PATH" go run main.go
  if [ $? -ne 0 ];
  then
  echo "Go integration test failed"
  exit 1
  fi
  popd
  unset SI_azBlobSASToken
  echo -e "Go integration test passed\n\n"
else
  echo "Go compiler not found. Skipping Go integration test."
fi

echo "Running integration test against blob with dynamic container mount"
pip3 install h5py
add_interception_config_blob /mnt/quiverfs true true
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d largeMultiMountDynamic -s -n 20 -p 4 -f"
if [ $? -ne 0 ]
then
echo "Blob Integration test failed"
exit 1
fi
echo -e "Blob Integration test with dynamic container mount passed\n\n"

echo "Running integration test against blob with dynamic account mount"
add_dynamic_mount_config /mnt/quiverfs
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d largeDynamicAccount -s -n 20 -p 4"
if [ $? -ne 0 ]
then
echo "Blob Integration test failed"
exit 1
fi
echo -e "Blob Integration test with dynamic account mount passed\n\n"

echo "================================================"
echo "SERVERLESS TESTS - COMPLETED SUCCESSFULLY"
echo "================================================"
