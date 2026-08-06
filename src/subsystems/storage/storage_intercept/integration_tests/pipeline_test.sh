#!/bin/bash

#===============================================================================
# STORAGE INTERCEPT INTEGRATION TEST PIPELINE
#===============================================================================
# This script runs comprehensive integration tests for the storage intercept
# system as part of the PR pipeline. It tests various configurations including:
# - Basic blob storage interception
# - Cache-enabled configurations (local and remote)
# - Multi-mount scenarios
# - Encryption capabilities
# - FUSE filesystem integration
#
# AUTHENTICATION CONTEXT (CI migration):
# This pipeline runs on CI hosted pools where the local IMDS endpoint (169.254.169.254) is unavailable.
# We have shifted from pulling Dynamic Managed Identity tokens (via IMDS) to "pushing" pre-generated
# Access Tokens (via Workload Identity Federation). The 'AzureCLI@2' task authenticates the session
# and injects the token as 'SI_azBlobAccessToken', which is then consumed by the test configuration.
#
# Script Structure:
# Section 1: Environment Setup and Configuration Detection
# Section 2: Configuration Generation Functions  
# Section 3: Cache Server Management Functions
# Section 4: Test Utility Functions
# Section 5: Performance and Benchmark Tests
# Section 6: Unit and Integration Tests (No Cache Server Required)
# Section 7: Cache-Enabled Tests (Snapshot and Auth Disabled)
# Section 8: Cache-Enabled Tests (Auth Enabled) 
# Section 9: Cache-Enabled Tests (Snapshot Enabled, Comprehensive Testing)
# Section 10: Specialized Integration Tests
#===============================================================================

#===============================================================================
# SECTION 1: ENVIRONMENT SETUP AND CONFIGURATION DETECTION
#===============================================================================

# 1.1 Script Path Detection and Repository Root Discovery
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
SCRIPT_NAME="$(basename "$(test -L "$0" && readlink "$0" || echo "$0")")"
REPOSITORY_ROOT="$(readlink -f ${SCRIPT_DIR}/../../../../../)"

print_redacted_interception_config() {
    awk '
        $1 ~ /(SASToken|AccessToken)$/ {
            print $1, "<redacted>"
            next
        }
        { print }
    ' "${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"
}

# 1.2 Cache Server Configuration Constants
CACHE_SERVER_PORT_RANGE_START=20100
CACHE_SERVER_PORT_RANGE_LENGTH=10000
export CACHE_SERVER_PORT=${CACHE_SERVER_PORT_RANGE_START}
CACHE_SERVER_PID=-1
CACHE_SERVER_PATH=/_cache
CACHE_LOCAL_SSD_PATH=/_localcache

# Clean up cache directories before starting tests
echo "Cleaning up cache directories..."
sudo rm -rf ${CACHE_SERVER_PATH} ${CACHE_LOCAL_SSD_PATH}
sudo mkdir -p ${CACHE_SERVER_PATH} ${CACHE_LOCAL_SSD_PATH}
sudo chmod 777 ${CACHE_SERVER_PATH} ${CACHE_LOCAL_SSD_PATH}
echo "Cache directories cleaned and ready: ${CACHE_SERVER_PATH}, ${CACHE_LOCAL_SSD_PATH}"

# 1.3 Cache Server Metrics Configuration
CACHESERVER_METRICS_PORT_START=9096
CACHESERVER_METRICS_PORT_LENGTH=10000
export CACHESERVER_METRICS_PORT="${CACHESERVER_METRICS_PORT_START}"

# 1.4 Build Configuration Detection
if [ -z "$1" ]
then
  BUILD_CONFIGURATION=Debug
else
  BUILD_CONFIGURATION=$1
fi

# 1.5 Build Architecture Detection
BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"

# 1.6 Python Virtual Environment Activation
source /venv/bin/activate

# 1.7 Storage Interceptor Library Path Configuration
STORAGE_INTERCEPTOR="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/libStorageIntercept.so"
STORAGE_INTERCEPT_CONFIG_FILE_PATH=/tmp/storageIntercept.config

# 1.7 Export Environment Variables for Test Framework
export STORAGE_INTERCEPT_USE_EXISTING_CONFIG=true

#===============================================================================
# SECTION 2: CONFIGURATION GENERATION FUNCTIONS
#===============================================================================

# 2.1 Blob Storage Configuration Builder with Prefix Support
# Generates blob storage configuration with optional mount prefixes for multi-mount scenarios
# Parameters:
#   $1: MOUNT_PATH - The local mount path for the storage
#   $2: ACCOUNT_NAME - Azure storage account name
#   $3: MOUNT_PREFIX - Prefix for configuration keys (empty for single mount)
#   $4: MOUNT_SAS_TOKEN_NAME - Environment variable name for SAS token (optional)
#   $5: IS_DYNAMIC - Boolean flag for dynamic container support
#   $6: IS_FUSE_ENABLED - Boolean flag for FUSE optimization settings
function append_interception_config_blob_prefix() {
    MOUNT_PATH=$1
    ACCOUNT_NAME=$2
    MOUNT_PREFIX=$3
    MOUNT_SAS_TOKEN_NAME=$4
    IS_DYNAMIC=$5
    IS_FUSE_ENABLED=$6

    echo "${MOUNT_PREFIX}storagePath ${MOUNT_PATH}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}type blob" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}azBlobStorageAccountName ${ACCOUNT_NAME}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}azBlobStorageEndpoint https://${ACCOUNT_NAME}.blob.core.windows.net" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}azBlobContainerName dataset" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}maxConnsPerBlobClient 200" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    # Authentication priority: SAS Token -> Access Token
    # Note: IMDS is unavailable in CI pools. We rely on WIF Access Token (SI_azBlobAccessToken).
    # See "AUTHENTICATION CONTEXT" in file header for details.
    if [ -n "${MOUNT_SAS_TOKEN_NAME}" ] && [ -n "${!MOUNT_SAS_TOKEN_NAME:-}" ]
    then
      # SAS Token name provided and the actual token is non-empty
      echo "DEBUG: Using SAS token authentication"
      echo "${MOUNT_PREFIX}azBlobSASToken \${${MOUNT_SAS_TOKEN_NAME}}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    elif [ -n "${SI_azBlobAccessToken:-}" ]
    then
      # Use access token (from Azure CLI authentication)
      echo "DEBUG: Using access token authentication" 
      echo "${MOUNT_PREFIX}azBlobAccessToken ${SI_azBlobAccessToken}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    else
      # No valid authentication method available - this will cause the storage client to fail gracefully
      echo "# No authentication configured - expecting authentication to be provided externally"
    fi

    if [ "$IS_DYNAMIC" = true ]
    then
      echo "${MOUNT_PREFIX}azBlobDynamicContainer true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    fi

    if [ "$IS_FUSE_ENABLED" = true ]
    then
      # Important to enable large prefetch for fuse mount as fuse reads are small (4k)
      # and large prefetch helps in reducing the number of requests to the cache/blob
      echo "${MOUNT_PREFIX}enableLargePrefetch true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "${MOUNT_PREFIX}prefetchWindowSizeInMB 32" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    fi
}

# Enables and configures cache request retries used by the remote cache client.
function append_cache_request_retry_config() {
  echo "cacheRetryMaxAttempts 3" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
  echo "cacheRetryBaseDelayMs 100" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
  echo "cacheRetryMaxDelayMs 1000" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
}

# 2.2 Dynamic Mount Configuration Generator
# Creates configuration for dynamic account/container mounting scenarios
# Parameters:
#   $1: MOUNT_PATH - The local mount path for dynamic storage mounting
function add_dynamic_mount_config() {
    MOUNT_PATH=$1

    # Clear existing configuration file
    rm -f ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    echo "storagePath ${MOUNT_PATH}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "type blob" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "azBlobDynamicAccount true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    
    # Add authentication configuration using same priority logic as main function
    echo "DEBUG: STORAGE_INTCPT_SAS_TOKEN='${STORAGE_INTCPT_SAS_TOKEN:+SET}'"
    echo "DEBUG: SI_azBlobAccessToken='${SI_azBlobAccessToken:+SET}'"
    if [ -n "${STORAGE_INTCPT_SAS_TOKEN:-}" ]; then
        echo "DEBUG: Using SAS token in dynamic mount"
        echo "azBlobSASToken ${STORAGE_INTCPT_SAS_TOKEN}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    elif [ -n "${SI_azBlobAccessToken:-}" ]; then
        echo "DEBUG: Using access token in dynamic mount"
        echo "azBlobAccessToken ${SI_azBlobAccessToken}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    fi
}

# 2.3 Basic Blob Storage Configuration Generator
# Creates standard blob storage interception configuration
# Supports both single-mount and multi-mount scenarios with optional FUSE optimizations
# Parameters:
#   $1: STORAGE_INTERCEPT_PATH - Primary mount path for storage interception
#   $2: TEST_MULTI_MOUNT - Boolean flag to enable multi-mount configuration (default: false)
#   $3: IS_FUSE_ENABLED - Boolean flag to enable FUSE optimizations (default: false)
function add_interception_config_blob() {
    STORAGE_INTERCEPT_PATH=$1
    TEST_MULTI_MOUNT=${2:false}
    IS_FUSE_ENABLED=${3:false}

    # Clear existing configuration file
    rm ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    # Note: STORAGE_INTCPT_SAS_TOKEN is set as an environment variable by the pipeline setup.
    # This provides SAS token authentication for storage account access.
    # See https://docs.microsoft.com/en-us/azure/devops/pipelines/process/variables?view=azure-devops&tabs=yaml%2Cbatch

    # Configure multi-mount or single-mount based on flag
    if [ "$TEST_MULTI_MOUNT" = true ]
    then
      # Multi-mount configuration: Two storage accounts with different settings
      echo "multiMountEnabled true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "mountName0 mount1" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      append_interception_config_blob_prefix ${STORAGE_INTERCEPT_PATH} storageinterceptionpr1 "mount1" STORAGE_INTCPT_SAS_TOKEN true $IS_FUSE_ENABLED

      echo "mountName1 mount2" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      append_interception_config_blob_prefix "${STORAGE_INTERCEPT_PATH}2" storageinterceptionpr2 "mount2" "" true $IS_FUSE_ENABLED
    else
      # Single-mount configuration: One storage account
      append_interception_config_blob_prefix ${STORAGE_INTERCEPT_PATH} storageinterceptionpr1 "" "" false $IS_FUSE_ENABLED
    fi

    echo "Created the storage interception config file. Its contents are =>"
    print_redacted_interception_config
}

# 2.4 Blob Storage with Local Cache Configuration Generator
# Extends basic blob configuration with local cache server integration
# Parameters:
#   $1: STORAGE_INTERCEPT_PATH - Primary mount path for storage interception
#   $2: TEST_MULTI_MOUNT - Boolean flag to enable multi-mount configuration (default: false)  
#   $3: ENABLE_CACHE_DISCOVERY - Boolean flag to enable cache server discovery (default: false)
function add_interception_config_blob_with_cache() {
    # Generate base blob configuration first
    add_interception_config_blob $1 $2
    TEST_MULTI_MOUNT=${2:false}
    ENABLE_CACHE_DISCOVERY=${3:false}

    # Local cache server configuration
    echo "attributesCacheUseLocalServer true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "attributesCacheServerPort ${CACHE_SERVER_PORT}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheServerPort ${CACHE_SERVER_PORT}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    # Cache enablement configuration (mount-specific vs global)
    if [ "$TEST_MULTI_MOUNT" = true ]
    then
      # Multi-mount: Enable cache for mount1 only
      echo "mount1cacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "mount1attributesCacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    else
      # Single-mount: Enable cache globally
      echo "cacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "attributesCacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    fi

    # Cache discovery configuration (for distributed cache scenarios)
    if [ "$ENABLE_CACHE_DISCOVERY" = true ]
    then
      echo "cacheEnableRemote true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "cacheServerDiscoveryEnabled true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "cacheServerDiscoveryIntervalInSeconds 5" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "cacheServerDiscoveryEndpoint 127.0.0.1" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    else
      echo "cacheEnableRemote false" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    fi

    append_cache_request_retry_config

    echo "Created the storage interception config file with cache enabled. Its contents are =>"
    print_redacted_interception_config
}

# 2.5 Blob Storage with Local Cache and Encryption Configuration Generator
# Extends blob configuration with local cache server integration and encryption support
# Parameters:
#   $1: STORAGE_INTERCEPT_PATH - Primary mount path for storage interception
#   $2: TEST_MULTI_MOUNT - Boolean flag to enable multi-mount configuration (default: false)
function add_interception_config_blob_with_cache_encryption() {
    add_interception_config_blob_with_cache $1 $2
    TEST_MULTI_MOUNT=${2:false}

    if [ "$TEST_MULTI_MOUNT" = true ]
    then
      echo "mount1cacheEncryptionEnabled true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "mount1cacheEncryptionKey somedummykey" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    else
      echo "cacheEncryptionEnabled true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "cacheEncryptionKey somedummykey" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    fi

    echo "Created the storage interception config file with encryption enabled. Its contents are =>"
    print_redacted_interception_config
}

# 2.6 Blob Storage with Remote Cache Configuration Generator
# Extends blob configuration with remote cache server integration
# Parameters:
#   $1: STORAGE_INTERCEPT_PATH - Primary mount path for storage interception
#   $2: TEST_MULTI_MOUNT - Boolean flag to enable multi-mount configuration (default: false)
#   $3: IS_FUSE_ENABLED - Boolean flag to enable FUSE optimizations (default: false)
function add_interception_config_blob_with_remote_cache() {
    add_interception_config_blob $1 $2 $3
    TEST_MULTI_MOUNT=${2:false}

    if [ "$TEST_MULTI_MOUNT" = true ]
    then
      echo "mount1cacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "mount1attributesCacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    else
      echo "cacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "attributesCacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    fi
    echo "attributesCacheUseRemoteServer true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheServerPort ${CACHE_SERVER_PORT}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheEnableRemote true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheServerList 127.0.0.1" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    append_cache_request_retry_config

    echo "Created the storage interception config file with remote attribute and data cache enabled. Its contents are =>"
    print_redacted_interception_config
}

# 2.7 Blob Storage with Local SSD Cache Configuration Generator
# Creates blob storage configuration with local SSD-based caching
# Parameters:
#   $1: STORAGE_INTERCEPT_PATH - Primary mount path for storage interception
function add_interception_config_blob_with_localssdcache() {
    add_interception_config_blob $1

    echo "cacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheUseLocalFS true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheLocalFSCachePath ${CACHE_LOCAL_SSD_PATH}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheLocalFSCacheStagingPath ${CACHE_LOCAL_SSD_PATH}/_temp" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    append_cache_request_retry_config

    echo "Created the storage interception config file with local ssd cache enabled. Its contents are =>"
    print_redacted_interception_config
}

#===============================================================================
# SECTION 3: CACHE SERVER MANAGEMENT FUNCTIONS
#===============================================================================

# 3.1 Cache Server Startup and Port Management
# Starts the cache server with automatic port detection to avoid conflicts
# Supports background execution with optional output redirection for logging
# Parameters:
#   $1: output_redirect_file - Optional file path for redirecting server output
function start_cache_server() {
  local output_redirect_file=$1

  # Check if cache server is already running and kill it
  pkill CacheServer

  # ensure cache storage path is present
  mkdir -p ${CACHE_SERVER_PATH}

  # Loop and find free port to listen
  isfree=$(netstat -taln | grep ":$CACHE_SERVER_PORT ")
  while [[ -n "$isfree" ]]; do
      export CACHE_SERVER_PORT=$(($CACHE_SERVER_PORT_RANGE_START + $RANDOM % $CACHE_SERVER_PORT_RANGE_LENGTH))
      isfree=$(netstat -taln | grep ":$CACHE_SERVER_PORT ")
  done
  echo "Cache server port set to ${CACHE_SERVER_PORT}."

  isfree=$(netstat -taln | grep ":$CACHESERVER_METRICS_PORT ")
  while [[ -n "$isfree" ]]; do
      export CACHESERVER_METRICS_PORT=$(($CACHESERVER_METRICS_PORT_START + $RANDOM % $CACHESERVER_METRICS_PORT_LENGTH))
      isfree=$(netstat -taln | grep ":$CACHESERVER_METRICS_PORT ")
  done
  echo "Cache server metrics port set to ${CACHESERVER_METRICS_PORT}."
  
  # Start cache server in the background and save pid
  CACHE_SERVER_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/CacheServer"
  CacheServerCommand="${CACHE_SERVER_EXE} ${CACHE_SERVER_PORT} ${CACHE_SERVER_PATH} 100000000000 10000000 1 $"

  echo "Starting CacheServer using command: ${CacheServerCommand}"
  if [[ -n "$output_redirect_file" ]]; then
    echo "Redirecting CacheServer output to ${output_redirect_file}"
    ${CacheServerCommand} &>$output_redirect_file &
  else
    ${CacheServerCommand} &
  fi

  CACHE_SERVER_PID=$!
  echo "Started CacheServer in the background, pid: ${CACHE_SERVER_PID}"
}

# 3.2 Cache Server Shutdown and Cleanup
# Stops the cache server process and optionally preserves cache data
# Parameters:
#   $1: keep_cache - Pass "keep_cache" to preserve cache data, otherwise it's deleted
function stop_cache_server() {
  echo "Stopping CacheServer with pid: ${CACHE_SERVER_PID}"
  kill -9 ${CACHE_SERVER_PID}

  if [[ $1 == "keep_cache" ]]; then
    echo "Keeping cache server storage path: ${CACHE_SERVER_PATH}"
  else
    rm -rf ${CACHE_SERVER_PATH}
  fi

  echo "Stopped CacheServer"
}

#===============================================================================
# SECTION 4: TEST UTILITY FUNCTIONS
#===============================================================================

# 4.1 Cache Server Liveness Verification
# Tests whether the cache server is responding to requests properly
# Uses the LivenessProbe utility to verify server health
function test_liveness() {
  export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"
  TEST_CLIENT_CMD="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/LivenessProbe"
  echo "Starting Cache Server Liveness test with command: ${TEST_CLIENT_CMD}"
  eval "${TEST_CLIENT_CMD}"
  if [ $? -ne 0 ]
  then
      echo "Cache Server Liveness failed"
      exit 1
  fi
  echo "Cache Server Liveness test completed with success"
  echo ""
}

# 4.2 Cache Server Integration Test Execution
# Runs comprehensive cache server integration tests using the test client
# Uses the cacheServerTestClient utility to validate server functionality
function run_cacheserver_test() {
  export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"
  TEST_CLIENT_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/cacheServerTestClient"
  TEST_CLIENT_CMD="${TEST_CLIENT_EXE}"

  echo "Starting Cache Server Test with command: ${TEST_CLIENT_CMD}"
  eval "${TEST_CLIENT_CMD}"
  if [ $? -ne 0 ]
  then
      echo "Cache Server Test failed"
      exit 1
  fi

  echo "Cache Server Test completed with success"
  echo ""
}

# 4.3 Local SSD Cache Cleanup Utility
# Removes all cached data from the local SSD cache directory
# Used to ensure clean state between test runs
function reset_local_ssd_cache() {
  echo "Clearing local ssd cache"
  if [[ -d ${CACHE_LOCAL_SSD_PATH} ]]; then
    rm -rf ${CACHE_LOCAL_SSD_PATH}
  fi

  echo "Cleared local ssd cache"
}

# 4.4 Non-Intercepted File Access Validation
# Verifies that the storage interceptor doesn't emit logs for files outside intercepted paths
# Ensures clean separation between intercepted and non-intercepted file operations
function __verify_nologs_unintercepted_() {
  testFile=/tmp/intcptLogsTest
  outFile=/tmp/intcptLogsTestOut

  rm -f $testFile
  echo "abcd" > $testFile

  add_interception_config_blob /mnt/quiverfs

  echo ""  
  echo "Running test to verify no logs are emitted for non-intercepted file read"
  echo "LD_PRELOAD=${STORAGE_INTERCEPTOR} cat $testFile > $outFile"
  LD_PRELOAD="${STORAGE_INTERCEPTOR}" cat $testFile > $outFile

  if grep -q "DACS" $outFile; then
    echo "Storage Interception logs emitted for non-intercepted file read"
    exit 1
  fi
}

#===============================================================================
# SECTION 5: PERFORMANCE AND BENCHMARK TESTS
#===============================================================================

# 5.1 Performance Benchmark Execution and Upload
# Runs performance benchmarks and uploads results to Application Insights
# Requires APP_INSIGHTS_KEY environment variable for telemetry upload
# Skipped if SKIP_PERF_BENCHMARK environment variable is set to "true"
function __run_perf_benchmark__() {

  if [ "$SKIP_PERF_BENCHMARK" = "true" ]; then
    echo "Skipping perf benchmark as SKIP_PERF_BENCHMARK is set to true"
    return
  fi
  echo "Running perf tracker benchmark"
  stop_cache_server

  # Clean up cache directory to prevent disk space issues
  rm -rf ${CACHE_SERVER_PATH}
  mkdir -p ${CACHE_SERVER_PATH}

  if [[ -z "${APP_INSIGHTS_KEY}" ]]; then
    echo "APP_INSIGHTS_KEY not set."
    echo "When running locally, ensure you export it (get it from Azure Portal > App Insights > dacs_perf_tracker_ai > Overview > Instrumentation Key)"
    exit 1
  fi

  if [[ -z "${GIT_COMMIT}" ||  -z "${GIT_BRANCH}" ]]; then
    echo "Setting git variables"
    GIT_BRANCH="$(git symbolic-ref --short HEAD)"
    GIT_COMMIT="$(git rev-parse HEAD)"
    GIT_COMMIT_MESSAGE="$(git log -1 --pretty=%B | head -n 1)"
  fi

  TEST_CLIENT_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/cacheServerTestClient "
  export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"

  start_cache_server /dev/null
  sleep 10

  perf_logs=$($TEST_CLIENT_EXE measureperf | tee /dev/tty 2>/dev/null || cat)
  echo "$perf_logs"
  uploader_args=$(grep "\-\-upload_throughput_mbps" <<< "$perf_logs")
  uploader_args+=" --git_branch \"$GIT_BRANCH\" --git_commit_hash \"$GIT_COMMIT\" --git_commit_message \"$GIT_COMMIT_MESSAGE\""

  eval "${SCRIPT_DIR}/storage_perf_app_insights_uploader.py $uploader_args" 

  if [ $? -ne 0 ]
  then
      echo "Perf tracker benchmark upload failed."
      exit 1
  fi

  stop_cache_server
  echo "Done running & uploading perf tracker benchmark"
}

#===============================================================================
# SECTION 6: UNIT TESTS AND INTEGRATION TESTS (NO CACHE SERVER REQUIRED)
#===============================================================================
# This section contains tests that run without requiring a cache server to be active.
# Tests include: basic unit tests, log verification, Go integration tests, and
# dynamic mount scenarios.

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
# Pin h5py to the last known-good release.
pip3 install 'h5py==3.15.1'
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

#===============================================================================
# SECTION 7: CACHE-ENABLED TESTS (SNAPSHOT AND AUTH DISABLED)
#===============================================================================
# This section tests cache server functionality with default configuration:
# - Snapshot generation: Disabled  
# - Authentication: Disabled
# - Tests: Performance clients, eviction, cache client library, Rust integration

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

stop_cache_server

#===============================================================================
# SECTION 8: CACHE-ENABLED TESTS (AUTHENTICATION ENABLED)
#===============================================================================
# This section tests cache server functionality with authentication enabled:
# - Snapshot generation: Disabled
# - Authentication: Enabled (using test credentials)
# - Tests: Cache client library with authentication validation

export CACHESERVER_AUTH_ENABLED="true"
export CACHESERVER_AUTH_ACCOUNTNAME="us3rn@m3"
CACHESERVER_AUTH_ACCOUNTKEY="$(head -c 24 /dev/urandom | base64)"
export CACHESERVER_AUTH_ACCOUNTKEY
start_cache_server /dev/null
sleep 10

echo "Running cache client lib test with authentication enabled"
cd ${REPOSITORY_ROOT}/build/$BUILD_CONFIGURATION/$BUILD_ARCH
eval "ctest -V --no-tests=error -j -1 -R CacheClientLibTests"
if [ $? -ne 0 ]
then
echo "cache client lib unit test with authentication failed"
exit 1
fi
echo -e "cache client lib unit test with authentication passed\n\n"

stop_cache_server
unset CACHESERVER_AUTH_ENABLED
unset CACHESERVER_AUTH_ACCOUNTNAME
unset CACHESERVER_AUTH_ACCOUNTKEY
echo ""

#===============================================================================
# SECTION 9: CACHE-ENABLED TESTS (SNAPSHOT ENABLED, COMPREHENSIVE TESTING)
#===============================================================================
# This section contains the most comprehensive cache server testing:
# - Snapshot generation: Enabled (1-second intervals)
# - Authentication: Disabled  
# - Eviction: Enabled (30-second frequency)
# - Tests: Full integration suite including cache, encryption, multi-mount,
#   remote cache, local SSD cache, and various storage configurations

# Start cache server in the background, with continuous snapshotting every second
# as well as eviction enabled, and eviction pass frequency set to 30s
# last access threshold defaults to 30 days however
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
echo ""
sleep 10

#test liveness of the cache server
test_liveness

# run the cache server test
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

#===============================================================================
# SECTION 10: SPECIALIZED VALIDATION AND INTEGRATION TESTS
#===============================================================================
# This section contains specialized tests for advanced features:
# - Snapshot validation and restart scenarios
# - Cache eviction validation and parallel operations  
# - Last access time distribution analysis
# - Future: ADLS (Azure Data Lake Storage) integration tests

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

# TODO: Future enhancement - Add comprehensive tests for ADLS (Azure Data Lake Storage)

source "${SCRIPT_DIR}/checksum_e2e_test.sh"
__run_checksum_e2e_test__ xxhash32
echo "All Storage interception integration tests completed successfully!"