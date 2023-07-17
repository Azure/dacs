#!/bin/bash

# This script is an integration test run as part of PR pipeline

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
SCRIPT_NAME="$(basename "$(test -L "$0" && readlink "$0" || echo "$0")")"
REPOSITORY_ROOT="$(readlink -f ${SCRIPT_DIR}/../../../../../)"

CACHE_SERVER_PORT_RANGE_START=20100
CACHE_SERVER_PORT_RANGE_LENGTH=10000
CACHE_SERVER_PORT=${CACHE_SERVER_PORT_RANGE_START}
CACHE_SERVER_PID=-1
CACHE_SERVER_PATH=/tmp/_cache
CACHE_LOCAL_SSD_PATH=/tmp/_localcache

if [ -z "$1" ]
then
  BUILD_CONFIGURATION=Debug
else
  BUILD_CONFIGURATION=$1
fi

function append_interception_config_blob_prefix() {
    STORAGE_INTERCEPT_CONFIG_FILE_PATH=/tmp/storageIntercept.config
    MOUNT_PATH=$1
    ACCOUNT_NAME=$2
    MOUNT_PREFIX=$3
    MOUNT_SAS_TOKEN_NAME=$4
    IS_DYNAMIC=$5

    echo "${MOUNT_PREFIX}storagePath ${MOUNT_PATH}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}type blob" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}azBlobStorageAccountName ${ACCOUNT_NAME}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}azBlobSASToken \${${MOUNT_SAS_TOKEN_NAME}}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}azBlobStorageEndpoint https://${ACCOUNT_NAME}.blob.core.windows.net" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}azBlobContainerName dataset" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "${MOUNT_PREFIX}maxConnsPerBlobClient 30" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    if [ "$IS_DYNAMIC" = true ]
    then
      echo "${MOUNT_PREFIX}azBlobDynamicContainer true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    fi
}

function add_dynamic_mount_config() {
    STORAGE_INTERCEPT_CONFIG_FILE_PATH=/tmp/storageIntercept.config
    MOUNT_PATH=$1

    rm ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    echo "storagePath ${MOUNT_PATH}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "type blob" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "azBlobDynamicAccount true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "azBlobSASToken \${STORAGE_INTCPT_SAS_TOKEN}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
}

function add_interception_config_blob() {
    STORAGE_INTERCEPT_CONFIG_FILE_PATH=/tmp/storageIntercept.config
    STORAGE_INTERCEPT_PATH=$1
    TEST_MULTI_MOUNT=${2:false}

    rm ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    # Note: STORAGE_INTERCEPT_SAS_TOKEN is set as a user-defined secret variable in the pipeline.
    # See https://docs.microsoft.com/en-us/azure/devops/pipelines/process/variables?view=azure-devops&tabs=yaml%2Cbatch

    
    if [ "$TEST_MULTI_MOUNT" = true ]
    then
      echo "multiMountEnabled true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "mountName0 mount1" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      append_interception_config_blob_prefix ${STORAGE_INTERCEPT_PATH} surkriseastus "mount1" STORAGE_INTCPT_SAS_TOKEN true

      echo "mountName1 mount2" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      append_interception_config_blob_prefix "${STORAGE_INTERCEPT_PATH}2" surkriseastus2 "mount2" STORAGE_INTCPT_SAS_TOKEN2 true
    else
      append_interception_config_blob_prefix ${STORAGE_INTERCEPT_PATH} surkriseastus "" STORAGE_INTCPT_SAS_TOKEN false
    fi

    echo "Created the storage interception config file. Its contents are =>"
    eval "cat ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"
}

function add_interception_config_blob_with_cache() {
    add_interception_config_blob $1 $2
    TEST_MULTI_MOUNT=${2:false}

    STORAGE_INTERCEPT_CONFIG_FILE_PATH=/tmp/storageIntercept.config
    if [ "$TEST_MULTI_MOUNT" = true ]
    then
      echo "mount1cacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "mount1attributesCacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    else
      echo "cacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
      echo "attributesCacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    fi
    echo "attributesCacheUseLocalServer true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "attributesCacheServerPort ${CACHE_SERVER_PORT}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheServerPort ${CACHE_SERVER_PORT}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheEnableRemote false" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    echo "Created the storage interception config file with cache enabled. Its contents are =>"
    eval "cat ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"
}

function add_interception_config_blob_with_cache_encryption() {
    add_interception_config_blob_with_cache $1 $2

    STORAGE_INTERCEPT_CONFIG_FILE_PATH=/tmp/storageIntercept.config
    echo "cacheEncryptionEnabled true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheEncryptionKey somedummykey" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    echo "Created the storage interception config file with encryption enabled. Its contents are =>"
    eval "cat ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"
}

function add_interception_config_blob_with_remote_cache() {
    add_interception_config_blob $1 $2
    TEST_MULTI_MOUNT=${2:false}

    STORAGE_INTERCEPT_CONFIG_FILE_PATH=/tmp/storageIntercept.config
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

    echo "Created the storage interception config file with remote attribute and data cache enabled"
}

function add_interception_config_blob_with_localssdcache() {
    add_interception_config_blob $1

    STORAGE_INTERCEPT_CONFIG_FILE_PATH=/tmp/storageIntercept.config
    echo "cacheEnable true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheUseLocalFS true" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheLocalFSCachePath ${CACHE_LOCAL_SSD_PATH}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "cacheLocalFSCacheStagingPath ${CACHE_LOCAL_SSD_PATH}/_temp" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    echo "Created the storage interception config file with local ssd cache enabled. Its contents are =>"
    eval "cat ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}"
}

function start_cache_server() {
  local output_redirect_file=$1

  # ensure cache storage path is present
  mkdir -p ${CACHE_SERVER_PATH}

  # Loop and find free port to listen
  isfree=$(netstat -taln | grep $CACHE_SERVER_PORT)
  while [[ -n "$isfree" ]]; do
      CACHE_SERVER_PORT=$(($CACHE_SERVER_PORT_RANGE_START + $RANDOM % $CACHE_SERVER_PORT_RANGE_LENGTH))
      isfree=$(netstat -taln | grep $CACHE_SERVER_PORT)
  done
  
  # Start cache server in the background and save pid
  CACHE_SERVER_EXE="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/linux-x64/publish-storage/storage/CacheServer"
  CacheServerCommand="${CACHE_SERVER_EXE} ${CACHE_SERVER_PORT} ${CACHE_SERVER_PATH} 100000000000 10000000 1"

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
  export TEST_CHECK_LIVENESS="true"
  TEST_CLIENT_EXE="${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/linux-x64/publish-storage/storage/cacheServerTestClient "
  TEST_CLIENT_CMD="${TEST_CLIENT_EXE}"
  echo "starting Cache Server Liveness test with command: ${TEST_CLIENT_CMD}"
  eval "${TEST_CLIENT_CMD}"
  if [ $? -ne 0 ]
  then
      echo "Cache Server Liveness failed"
      exit 1
  fi
  echo "Cache Server Liveness test completed with success"
}

function reset_local_ssd_cache() {
  echo "Clearing local ssd cache"
  if [[ -d ${CACHE_LOCAL_SSD_PATH} ]]; then
    rm -rf ${CACHE_LOCAL_SSD_PATH}
  fi

  echo "Cleared local ssd cache"
}

echo "Running cache server unit tests"
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/non_cuda/
eval "ctest -V --no-tests=error -j -1 -R CacheServer"
if [ $? -ne 0 ]
then
    echo "Cache server unit tests failed"
    exit 1
fi

echo "Running unit tests against blob"
add_interception_config_blob /mnt/dataPathIntcpt
export STORAGE_INTERCEPT_USE_EXISITNG_CONFIG=true
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/non_cuda/
eval "ctest -V --no-tests=error -j -1 -R StorageIntercept"
if [ $? -ne 0 ]
then
    echo "Unit tests against blob failed"
    exit 1
fi
echo ""

echo "Running go integration test against blob"
add_interception_config_blob /mnt/quiverfs
export SI_azBlobSASToken="incorrect-token"
# Install go
pushd /tmp
wget https://go.dev/dl/go1.20.5.linux-amd64.tar.gz
rm -rf /usr/local/go && tar -C /usr/local -xzf go1.20.5.linux-amd64.tar.gz
export PATH=$PATH:/usr/local/go/bin
popd

# Run the test
export DACS_PATH=${REPOSITORY_ROOT}/obj/${BUILD_CONFIGURATION}/native/linux-x64/publish-storage/storage/
pushd ${SCRIPT_DIR}
LD_LIBRARY_PATH=$DACS_PATH CGO_CFLAGS="-I $DACS_PATH/include" CGO_LDFLAGS="-L $DACS_PATH" go run main.go
if [ $? -ne 0 ];
then
    echo "Go integration test failed"
    exit 1
fi
popd
unset SI_azBlobSASToken
echo ""

echo "Running integration test against blob with dynamic container mount"
pip3 install h5py
add_interception_config_blob /mnt/quiverfs true
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d largeMultiMountDynamic -s -n 20 -p 4"
if [ $? -ne 0 ]
then
    echo "Blob Integration test failed"
    exit 1
fi
echo ""

echo "Running integration test against blob with dynamic account mount"
add_dynamic_mount_config /mnt/quiverfs
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d largeDynamicAccount -s -n 20 -p 4"
if [ $? -ne 0 ]
then
    echo "Blob Integration test failed"
    exit 1
fi
echo ""

# Start cache server in the background, with continuous snapshotting every second
export CACHESERVER_HOSTNAME="testhost"
export CACHESERVER_SNAPSHOT_INTERVAL="1"
start_cache_server
unset CACHESERVER_HOSTNAME
unset CACHESERVER_SNAPSHOT_INTERVAL
echo "Sleeping 30 secs to allow cache server to start and initialize"
echo ""
sleep 30

#test liveness of the cache server
test_liveness

echo "Running unit tests against blob with cache enabled"
add_interception_config_blob_with_cache /mnt/dataPathIntcpt
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/non_cuda/
eval "ctest -V --no-tests=error -j -1 -R StorageIntercept"
if [ $? -ne 0 ]
then
    echo "Unit tests against blob with cache failed"
    exit 1
fi
echo ""

echo "Running integration test against blob with cache"
add_interception_config_blob_with_cache /mnt/quiverfs
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d large -s -n 20 -p 4"
if [ $? -ne 0 ]
then
    echo "Blob Integration test with cache failed"
    exit 1
fi
echo ""

echo "Running unit tests against blob with cache and encryption enabled"
add_interception_config_blob_with_cache_encryption /mnt/dataPathIntcpt
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/non_cuda/
eval "ctest -V --no-tests=error -j -1 -R StorageIntercept"
if [ $? -ne 0 ]
then
    echo "Unit tests against blob with cache and encryption failed"
    exit 1
fi
echo ""

echo "Running integration test against blob with cache and encryption"
add_interception_config_blob_with_cache_encryption /mnt/quiverfs true
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d largeMultiMountDynamic -s -n 20 -p 4"
if [ $? -ne 0 ]
then
    echo "Blob Integration test with cache and encryption failed"
    exit 1
fi
echo ""

echo "Running unit tests against blob with remote attribute and data cache enabled"
add_interception_config_blob_with_remote_cache /mnt/dataPathIntcpt
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/non_cuda/
eval "ctest -V --no-tests=error -j -1 -R StorageIntercept"
if [ $? -ne 0 ]
then
    echo "Unit tests against blob with remote attribute and data cache failed"
    exit 1
fi
echo ""

echo "Running integration test against blob with remote attribute and data cache enabled"
add_interception_config_blob_with_remote_cache /mnt/quiverfs
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d large -s -n 20 -p 4"
if [ $? -ne 0 ]
then
    echo "Blob Integration test with remote attribute and data cache failed"
    exit 1
fi
echo ""

echo "Running integration test with multi-mount against blob with remote attribute and data cache enabled"
add_interception_config_blob_with_remote_cache /mnt/quiverfs true
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d largeMultiMountDynamic -s -n 20 -p 4"
if [ $? -ne 0 ]
then
    echo "Blob Integration test with multi-mount and remote attribute and data cache failed"
    exit 1
fi
echo ""

echo "Running unit tests against blob with local ssd cache"
reset_local_ssd_cache
add_interception_config_blob_with_localssdcache /mnt/dataPathIntcpt
cd ${REPOSITORY_ROOT}/bin/$BUILD_CONFIGURATION/native/non_cuda/
eval "ctest -V --no-tests=error -j -1 -R StorageIntercept"
if [ $? -ne 0 ]
then
    echo "Unit tests against blob with local ssd cache failed"
    exit 1
fi
echo ""

echo "Running integration test against blob with local ssd cache"
reset_local_ssd_cache
add_interception_config_blob_with_localssdcache /mnt/quiverfs
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d large -s -n 20 -p 4"
if [ $? -ne 0 ]
then
    echo "Blob Integration test with local ssd cache failed"
    exit 1
fi
echo ""

reset_local_ssd_cache
stop_cache_server

echo "Running snapshot validation tests"
source "$SCRIPT_DIR/snapshot_integration_test.sh"
__run_restart_snapshot_basic_test__
__test_snapshot_disk_mismatch__
__test_no_crash_on_invalid_snapshot__
__test_upload_download_snapshot_in_parallel__
__test_no_crash_on_truncated_snapshot__
echo "Snapshot validation tests passed"

# TODO: add tests for adls
echo "All Storage interception integration tests completed"