#!/bin/bash
BUILD_ARCH="${BUILD_ARCH:-amd64}"
# This script is useful for testing the python storage client

# print commands and their arguments as they are executed
set -x

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
REPOSITORY_ROOT=$(realpath $SCRIPT_DIR/../../../../..)
BUILD_CONFIGURATION=Debug

# Helper functions

# Cache Server Configuration Constants
CACHE_SERVER_PORT_RANGE_START=20100
CACHE_SERVER_PORT_RANGE_LENGTH=10000
export CACHE_SERVER_PORT=${CACHE_SERVER_PORT_RANGE_START}
export CACHE_SERVER_LIST="127.0.0.1"
export CACHESERVER_METRICS_PORT=9096
CACHE_SERVER_PID=-1
CACHE_SERVER_PATH=/tmp/_cache
CACHE_LOCAL_SSD_PATH=/tmp/_localcache

# dev VMs have limited resources, so limiting server\client concurrency to avoid overload
export CACHESERVER_THREADPOOL_SIZE=80
export SI_maxConnsPerBlobClient=44

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
  isfree=$(netstat -taln | grep $CACHE_SERVER_PORT)
  while [[ -n "$isfree" ]]; do
      export CACHE_SERVER_PORT=$(($CACHE_SERVER_PORT_RANGE_START + $RANDOM % $CACHE_SERVER_PORT_RANGE_LENGTH))
      isfree=$(netstat -taln | grep $CACHE_SERVER_PORT)
  done
  
  # Start cache server in the background and save pid
  CACHE_SERVER_EXE="${REPOSITORY_ROOT}/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage/CacheServer"
  TTL_WINDOW_IN_SECONDS=5
  CacheServerCommand="${CACHE_SERVER_EXE} ${CACHE_SERVER_PORT} ${CACHE_SERVER_PATH} 100000000000 10000000 ${TTL_WINDOW_IN_SECONDS}"

  echo "Starting CacheServer using command: ${CacheServerCommand}"
  if [[ -n "$output_redirect_file" ]]; then
    echo "Redirecting CacheServer output to ${output_redirect_file}"
    ${CacheServerCommand} &>$output_redirect_file &
  else
    echo "CacheServer output not redirected"
    ${CacheServerCommand} &
  fi

  CACHE_SERVER_PID=$!
  echo "Started CacheServer in the background, pid: ${CACHE_SERVER_PID}"
}

# Cache Server Shutdown and Cleanup
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

function install_dependencies() {
  # install required dependencies
  pip3 install requests # This can be removed once the token fetch code inside python package is removed
}

# change to the package parent directory
ORIGNAL_DIR=$(pwd)
cd $SCRIPT_DIR/..

# copy the intercept library to package directory
cp -v $REPOSITORY_ROOT/obj/$BUILD_CONFIGURATION/native/linux-x64/publish-storage/storage/libStorageDirect.so ./py_dacs_cache_client/

# install dependencies
install_dependencies

# start the cache server
start_cache_server $REPOSITORY_ROOT/out_test_py_client_cache_server.log

# run the tests
python3 -m py_dacs_cache_client.sample.dacs_cache_client_test --nBlobs 10 --blobSizeToBeRead 1073741824

# cleanup
stop_cache_server
rm -f ./py_dacs_cache_client/libStorageDirect.so

# go back to original directory
cd $ORIGNAL_DIR
