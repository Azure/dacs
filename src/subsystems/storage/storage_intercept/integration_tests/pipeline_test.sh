#!/bin/bash

# This script is an integration test run as part of PR pipeline

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
SCRIPT_NAME="$(basename "$(test -L "$0" && readlink "$0" || echo "$0")")"
REPOSITORY_ROOT="$(readlink -f ${SCRIPT_DIR}/../../../../../)"

if [ -z "$1" ]
then
  BUILD_CONFIGURATION=Debug
else
  BUILD_CONFIGURATION=$1
fi

function add_interception_config_blob() {
    STORAGE_INTERCEPT_CONFIG_FILE_PATH=/tmp/storageIntercept.config
    STORAGE_INTERCEPT_PATH=$1

    rm ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    # Note: STORAGE_INTERCEPT_SAS_TOKEN is set as a user-defined secret variable in the pipeline.
    # See https://docs.microsoft.com/en-us/azure/devops/pipelines/process/variables?view=azure-devops&tabs=yaml%2Cbatch

    echo "storagePath ${STORAGE_INTERCEPT_PATH}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "type blob" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "azBlobStorageAccountName surkriseastus" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "azBlobSASToken \${STORAGE_INTCPT_SAS_TOKEN}" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "azBlobStorageEndpoint https://surkriseastus.blob.core.windows.net" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "azBlobContainerName dataset" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}
    echo "maxConnsPerBlobClient 30" >> ${STORAGE_INTERCEPT_CONFIG_FILE_PATH}

    echo "Created the storage interception config file"
}

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

echo "Running integration test against blob"
pip3 install h5py
add_interception_config_blob /mnt/quiverfs
eval "${SCRIPT_DIR}/synthetic_test_runner.sh -b ${BUILD_CONFIGURATION} -d large -n 20 -p 4"
if [ $? -ne 0 ]
then
    echo "Blob Integration test failed"
    exit 1
fi
echo ""

# TODO: add tests for adls
echo "All Storage interception integration tests completed"