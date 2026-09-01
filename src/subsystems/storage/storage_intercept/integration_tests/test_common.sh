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
# COMMON TEST UTILITIES AND FUNCTIONS
#===============================================================================
# This script contains shared utility functions used by all storage intercept
# integration test scripts. It should be sourced by each test script.
#
# Contents:
# - Environment setup and configuration detection
# - Configuration generation functions
# - Cache server management functions
# - Test utility functions
#
# AUTHENTICATION CONTEXT (CI migration):
# This script runs on CI hosted pools where the local IMDS endpoint (169.254.169.254) is unavailable.
# We have shifted from pulling Dynamic Managed Identity tokens (via IMDS) to "pushing" pre-generated
# Access Tokens (via Workload Identity Federation). The 'AzureCLI@2' task authenticates the session
# and injects the token as 'SI_azBlobAccessToken', which is then consumed by the test configuration.
#===============================================================================

#===============================================================================
# SECTION 1: ENVIRONMENT SETUP AND CONFIGURATION DETECTION
#===============================================================================

# 1.1 Script Path Detection and Repository Root Discovery
SCRIPT_DIR="$( cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 ; pwd -P )"
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
# Atomically create unique paths because concurrent containers can have the
# same PID while sharing the host-backed TMPDIR mounted by bldenv.sh.
if [[ -z "${CACHE_SERVER_PATH:-}" ]]; then
  CACHE_SERVER_PATH="$(mktemp -d "${TMPDIR:-/tmp}/dacs-cache-server.XXXXXX")"
fi
if [[ -z "${CACHE_LOCAL_SSD_PATH:-}" ]]; then
  CACHE_LOCAL_SSD_PATH="$(mktemp -d "${TMPDIR:-/tmp}/dacs-local-cache.XXXXXX")"
fi

# 1.3 Cache Server Metrics Configuration
CACHESERVER_METRICS_PORT_START=9096
CACHESERVER_METRICS_PORT_LENGTH=10000
export CACHESERVER_METRICS_PORT="${CACHESERVER_METRICS_PORT_START}"

# 1.4 Build Configuration Detection
# Use BUILD_CONFIGURATION if already set, otherwise default to Debug
BUILD_CONFIGURATION="${BUILD_CONFIGURATION:-Debug}"

# 1.5 Build Architecture Detection
BUILD_ARCH="${BUILD_ARCH:-amd64}"
DACS_AZURE_STORAGE_ACCOUNT="${DACS_AZURE_STORAGE_ACCOUNT:-dacstest}"
DACS_AZURE_STORAGE_CONTAINER="${DACS_AZURE_STORAGE_CONTAINER:-dataset}"
DACS_AZURE_STORAGE_SECONDARY_CONTAINER="${DACS_AZURE_STORAGE_SECONDARY_CONTAINER:-dataset2}"

# 1.6 Python Virtual Environment Activation
if [ -f /venv/bin/activate ]; then
  source /venv/bin/activate
else
  echo "Warning: /venv/bin/activate not found, skipping venv activation"
fi

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
#   $3: CONTAINER_NAME - Azure Blob container name
#   $4: MOUNT_PREFIX - Prefix for configuration keys (empty for single mount)
#   $5: MOUNT_SAS_TOKEN_NAME - Environment variable name for SAS token (optional)
#   $6: IS_DYNAMIC - Boolean flag for dynamic container support
#   $7: IS_FUSE_ENABLED - Boolean flag for FUSE optimization settings
function append_interception_config_blob_prefix() {
    MOUNT_PATH=$1
    ACCOUNT_NAME=$2
    CONTAINER_NAME=$3
    MOUNT_PREFIX=$4
    MOUNT_SAS_TOKEN_NAME=$5
    IS_DYNAMIC=$6
    IS_FUSE_ENABLED=$7

    echo "${MOUNT_PREFIX}storagePath ${MOUNT_PATH}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}type blob" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}azBlobStorageAccountName ${ACCOUNT_NAME}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}azBlobStorageEndpoint https://${ACCOUNT_NAME}.blob.core.windows.net" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}azBlobContainerName ${CONTAINER_NAME}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}maxConnsPerBlobClient 200" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    # Exercise the azure-sdk-for-cpp blob client with Azure CRC64 validation.
    echo "${MOUNT_PREFIX}remoteBlobUseSdkClient true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}remoteBlobChecksumEnabled true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

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
    rm -f ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    # Note: STORAGE_INTCPT_SAS_TOKEN is set as an environment variable by the pipeline setup.
    # This provides SAS token authentication for storage account access.
    # See https://docs.microsoft.com/en-us/azure/devops/pipelines/process/variables?view=azure-devops&tabs=yaml%2Cbatch

    # Configure multi-mount or single-mount based on flag
    if [ "$TEST_MULTI_MOUNT" = true ]
    then
      # Multi-mount configuration: Two containers with different mount settings
      echo "multiMountEnabled true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "mountName0 mount1" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      append_interception_config_blob_prefix ${STORAGE_INTERCEPT_PATH} "${DACS_AZURE_STORAGE_ACCOUNT}" "${DACS_AZURE_STORAGE_CONTAINER}" "mount1" STORAGE_INTCPT_SAS_TOKEN true $IS_FUSE_ENABLED

      echo "mountName1 mount2" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      append_interception_config_blob_prefix "${STORAGE_INTERCEPT_PATH}2" "${DACS_AZURE_STORAGE_ACCOUNT}" "${DACS_AZURE_STORAGE_SECONDARY_CONTAINER}" "mount2" "" true $IS_FUSE_ENABLED
    else
      # Single-mount configuration: One storage account
      append_interception_config_blob_prefix ${STORAGE_INTERCEPT_PATH} "${DACS_AZURE_STORAGE_ACCOUNT}" "${DACS_AZURE_STORAGE_CONTAINER}" "" "" false $IS_FUSE_ENABLED
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
  pkill CacheServer || true

  # Increase fd limit for splice() zero-copy path which uses pipe fds per upload
  ulimit -n 65536 2>/dev/null || ulimit -n 8192 2>/dev/null || echo "Warning: could not increase fd limit (current: $(ulimit -n))"
  echo "File descriptor limit: $(ulimit -n)"

  # ensure cache storage path is present
  mkdir -p ${CACHE_SERVER_PATH}

  # Loop and find free port to listen
  isfree=$(netstat -taln | grep ":$CACHE_SERVER_PORT " || true)
  while [[ -n "$isfree" ]]; do
      export CACHE_SERVER_PORT=$(($CACHE_SERVER_PORT_RANGE_START + $RANDOM % $CACHE_SERVER_PORT_RANGE_LENGTH))
      isfree=$(netstat -taln | grep ":$CACHE_SERVER_PORT " || true)
  done
  echo "Cache server port set to ${CACHE_SERVER_PORT}."

  isfree=$(netstat -taln | grep ":$CACHESERVER_METRICS_PORT " || true)
  while [[ -n "$isfree" ]]; do
      export CACHESERVER_METRICS_PORT=$(($CACHESERVER_METRICS_PORT_START + $RANDOM % $CACHESERVER_METRICS_PORT_LENGTH))
      isfree=$(netstat -taln | grep ":$CACHESERVER_METRICS_PORT " || true)
  done
  echo "Cache server metrics port set to ${CACHESERVER_METRICS_PORT}."

  # Start cache server in the background and save pid
  CACHE_SERVER_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/CacheServer"
  CacheServerCommand="${CACHE_SERVER_EXE} --port ${CACHE_SERVER_PORT} --cacheRoot ${CACHE_SERVER_PATH} --sizeLimitBytes 100000000000 --ttlWindowSeconds 1"

  # Set log level from environment variable (CRITICAL, ERROR, WARN, INFO, DEBUG, TRACE)
  export LOG_LEVEL="${LOG_LEVEL:-INFO}"

  echo "Starting CacheServer using command: ${CacheServerCommand}"
  echo "Log level: ${LOG_LEVEL}"
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
  if [[ ${CACHE_SERVER_PID} -ne -1 ]]; then
    echo "Stopping CacheServer with pid: ${CACHE_SERVER_PID}"
    kill -9 ${CACHE_SERVER_PID} || true
    CACHE_SERVER_PID=-1
  else
    echo "No CacheServer running (PID is -1)"
  fi

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
