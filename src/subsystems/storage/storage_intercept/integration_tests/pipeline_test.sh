#!/bin/bash

# This script is an integration test run as part of PR pipeline

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
SCRIPT_NAME="$(basename "$(test -L "$0" && readlink "$0" || echo "$0")")"
REPOSITORY_ROOT="$(readlink -f ${SCRIPT_DIR}/../../../../../)"

CACHE_SERVER_PORT_RANGE_START=20100
CACHE_SERVER_PORT_RANGE_LENGTH=10000
export CACHE_SERVER_PORT=${CACHE_SERVER_PORT_RANGE_START}
CACHE_SERVER_PID=-1
CACHE_SERVER_PATH=/_cache
CACHE_LOCAL_SSD_PATH=/_localcache
# set the cache server metrics port to a 9096 for testing
export CACHESERVER_METRICS_PORT="9096"
echo "cache server metrics port set to ${CACHESERVER_METRICS_PORT}"

if [ -z "$1" ]
then
  BUILD_CONFIGURATION=Debug
else
  BUILD_CONFIGURATION=$1
fi

# Detect architecture for proper library paths
ARCH=$(uname -m)
case $ARCH in
    x86_64)
        PLATFORM_ARCH="linux-x64"
        ;;
    aarch64|arm64)
        PLATFORM_ARCH="linux-arm64"
        ;;
    *)
        echo "Unsupported architecture: $ARCH"
        exit 1
        ;;
esac
echo "Detected architecture: $ARCH, using platform: $PLATFORM_ARCH"

source /venv/bin/activate

STORAGE_INTERCEPTOR="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/${PLATFORM_ARCH}/publish-storage/storage/libStorageIntercept.so"
STORAGE_INTERCEPT_CONFIG_FILE_PATH=/tmp/storageIntercept.config

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
      # and large prefetch helps in reducing the number of requests to the cache\blob
      echo "${MOUNT_PREFIX}enableLargePrefetch true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "${MOUNT_PREFIX}prefetchWindowSizeInMB 32" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    fi
}

function add_dynamic_mount_config() {
    MOUNT_PATH=$1

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

function add_interception_config_blob() {
    STORAGE_INTERCEPT_PATH=$1
    TEST_MULTI_MOUNT=${2:false}
    IS_FUSE_ENABLED=${3:false}

    rm ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    # Note: STORAGE_INTCPT_SAS_TOKEN is set as an environment variable by the pipeline setup.
    # This provides SAS token authentication for storage account access.
    # See https://docs.microsoft.com/en-us/azure/devops/pipelines/process/variables?view=azure-devops&tabs=yaml%2Cbatch
    
    if [ "$TEST_MULTI_MOUNT" = true ]
    then
      echo "multiMountEnabled true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "mountName0 mount1" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      append_interception_config_blob_prefix ${STORAGE_INTERCEPT_PATH} storageinterceptionpr1 "mount1" STORAGE_INTCPT_SAS_TOKEN true $IS_FUSE_ENABLED

      echo "mountName1 mount2" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      append_interception_config_blob_prefix "${STORAGE_INTERCEPT_PATH}2" storageinterceptionpr2 "mount2" "" true $IS_FUSE_ENABLED
    else
      append_interception_config_blob_prefix ${STORAGE_INTERCEPT_PATH} storageinterceptionpr1 "" "" false $IS_FUSE_ENABLED
    fi

    echo "Created the storage interception config file. Its contents are =>"
    eval "cat ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"
}

function add_interception_config_blob_with_cache() {
    add_interception_config_blob $1 $2
    TEST_MULTI_MOUNT=${2:false}
    ENABLE_CACHE_DISCOVERY=${3:false}

    echo "attributesCacheUseLocalServer true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "attributesCacheServerPort ${CACHE_SERVER_PORT}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheServerPort ${CACHE_SERVER_PORT}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    if [ "$TEST_MULTI_MOUNT" = true ]
    then
      echo "mount1cacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "mount1attributesCacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    else
      echo "cacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "attributesCacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    fi

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
    eval "cat ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"
}

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
    eval "cat ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"
}

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
    eval "cat ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"
}

function add_interception_config_blob_with_localssdcache() {
    add_interception_config_blob $1

    echo "cacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheUseLocalFS true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheLocalFSCachePath ${CACHE_LOCAL_SSD_PATH}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheLocalFSCacheStagingPath ${CACHE_LOCAL_SSD_PATH}/_temp" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    echo "Created the storage interception config file with local ssd cache enabled. Its contents are =>"
    eval "cat ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"
}

function start_cache_server() {
  local output_redirect_file=$1

  # Check if cache server is already running and kill it
  pkill CacheServer

  # ensure cache storage path is present
  mkdir -p ${CACHE_SERVER_PATH}

  # Loop and find free port to listen
  isfree=$(netstat -taln | grep $CACHE_SERVER_PORT)
  while [[ -n "$isfree" ]]; do
      export CACHE_SERVER_PORT=$(($CACHE_SERVER_PORT_RANGE_START + $RANDOM % $CACHE_SERVER_PORT_RANGE_LENGTH))
      isfree=$(netstat -taln | grep $CACHE_SERVER_PORT)
  done
  
  # Start cache server in the background and save pid
  CACHE_SERVER_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/native-storage/storage/CacheServer"
  TTL_WINDOW_IN_SECONDS=5
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

function test_liveness() {
  export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"
  TEST_CLIENT_CMD="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/${PLATFORM_ARCH}/publish-storage/storage/LivenessProbe"
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

function run_cacheserver_test() {
  export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"
  TEST_CLIENT_EXE="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/${PLATFORM_ARCH}/publish-storage/storage/cacheServerTestClient"
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

function reset_local_ssd_cache() {
  echo "Clearing local ssd cache"
  if [[ -d ${CACHE_LOCAL_SSD_PATH} ]]; then
    rm -rf ${CACHE_LOCAL_SSD_PATH}
  fi

  echo "Cleared local ssd cache"
}

# Verify no logs are emitted for non-intercepted file read
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

function __run_perf_benchmark__() {

  if [ "$SKIP_PERF_BENCHMARK" = "true" ]; then
    echo "Skipping perf benchmark as SKIP_PERF_BENCHMARK is set to true"
    return
  fi
  echo "Running perf tracker benchmark"
  stop_cache_server

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

  TEST_CLIENT_EXE="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/${PLATFORM_ARCH}/publish-storage/storage/cacheServerTestClient "
  export TEST_CACHESERVER_ADDR_PORT="${CACHE_SERVER_PORT}"

  start_cache_server /dev/null
  sleep 10

  perf_logs=$($TEST_CLIENT_EXE measureperf | tee /dev/tty)
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

# Enable existing config
export STORAGE_INTERCEPT_USE_EXISTING_CONFIG=true

#### BEGIN : Tests which DONT require cache server running ####

__run_perf_benchmark__

echo "Running cache server unit tests"
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/storage/
eval "ctest -V --no-tests=error -j -1 -R CacheServer"
if [ $? -ne 0 ]
then
echo "Cache server unit tests failed"
exit 1
fi

__verify_nologs_unintercepted_

echo "Running unit tests against blob"
add_interception_config_blob /mnt/dataPathIntcpt
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/storage/
eval "ctest -V --no-tests=error -j -1 -R StorageIntercept"
if [ $? -ne 0 ]
then
echo "Unit tests against blob failed"
exit 1
fi
echo -e "Unit tests against blob passed\n\n"

echo "Running go integration test against blob"
add_interception_config_blob /mnt/quiverfs
export SI_azBlobSASToken="incorrect-token"

# Run the test
export DACS_PATH=${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/${PLATFORM_ARCH}/publish-storage/storage/
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

#### END : Tests which DONT require cache server running ####

#### BEGIN : Tests against cache server with snapshot and auth disabled ####
start_cache_server /dev/null
sleep 10

# Ensure perf client is wired up correctly
PERF_CLIENT_EXE="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/${PLATFORM_ARCH}/publish-storage/storage/cache-test/PerfClient "
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
EVICTION_CLIENT_EXE="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/${PLATFORM_ARCH}/publish-storage/storage/cache-test/PerfClient "
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
#END: Ensure eviction client is wired up correctly 

#Ensure last access distribution client is wired up correctly
LAST_ACCESS_DISTRIBUTION_CLIENT_EXE="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/${PLATFORM_ARCH}/publish-storage/storage/cache-test/PerfClient "
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
#END: Ensure last access distribution client is wired up correctly

echo "Running cache client lib test"
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/storage/
eval "ctest -V --no-tests=error -j -1 -R CacheClientLibTests"
if [ $? -ne 0 ]
then
echo "cache client lib unit test failed"
exit 1
fi
echo -e "cache client lib unit test passed\n\n"

echo "Running rust cache client integration test"

pushd ${REPOSITORY_ROOT}/src/subsystems/storage/cache_client/rust_tests
export LD_LIBRARY_PATH="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/${PLATFORM_ARCH}/publish-storage-cache-client:${LD_LIBRARY_PATH}"
export LIBRARY_PATH="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/${PLATFORM_ARCH}/publish-storage-cache-client"
export CACHE_CLIENT_LIB_DIR="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/${PLATFORM_ARCH}/publish-storage-cache-client"
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
### END : Tests against cache server with snapshot and auth disabled ####

### BEGIN : Tests against cache server with auth enabled but snapshot disabled ####
export CACHESERVER_AUTH_ENABLED="true"
export CACHESERVER_AUTH_ACCOUNTNAME="us3rn@m3"
export CACHESERVER_AUTH_ACCOUNTKEY="p@ssw0rd"
start_cache_server /dev/null
sleep 10

echo "Running cache client lib test with authentication enabled"
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/storage/
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
#### END : Tests against cache server with auth enabled but snapshot disabled ####

#### BEGIN : Tests against cache server with snapshot enabled but auth disabled ####
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
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/storage/
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
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/storage/
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
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/storage/
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
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/storage/
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
#### END : Tests against cache server with snapshot enabled but auth disabled ####

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


# TODO: add tests for adls
echo "All Storage interception integration tests completed"