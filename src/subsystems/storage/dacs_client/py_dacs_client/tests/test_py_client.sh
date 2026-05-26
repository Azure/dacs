#!/bin/bash
BUILD_ARCH="${BUILD_ARCH:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"
# This script is useful for testing the python storage client

# print commands and their arguments as they are executed
set -x

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
REPOSITORY_ROOT=$(realpath $SCRIPT_DIR/../../../../../..)
BUILD_CONFIGURATION=Debug

# Helper functions

# Cache Server Configuration Constants
CACHE_SERVER_PORT_RANGE_START=20100
CACHE_SERVER_PORT_RANGE_LENGTH=10000
export CACHE_SERVER_PORT=${CACHE_SERVER_PORT_RANGE_START}
export CACHE_SERVER_LIST="127.0.0.1"
CACHESERVER_METRICS_PORT_START=9096
CACHESERVER_METRICS_PORT_LENGTH=10000
export CACHESERVER_METRICS_PORT="${CACHESERVER_METRICS_PORT_START}"
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
  
  # Wait for server to be ready (timeout: 30s)
  for i in {1..30}; do
    nc -z 127.0.0.1 ${CACHE_SERVER_PORT} 2>/dev/null && break
    sleep 1
  done
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
  
  # Cleanup discovery config files
  sudo rm -f /serverConfig/cache_server_count /serverConfig/cache_server_endpoint_format
  echo "Cleaned up discovery config files"

  echo "Stopped CacheServer"
}

# Generate unique datetime suffix for blob naming
function generate_datetime_suffix() {
  echo $(date +%Y%m%d_%H%M%S)
}

# change to the package parent directory
ORIGNAL_DIR=$(pwd)
cd $SCRIPT_DIR/..

# -----------------------------------------------
# Test mode: source (default) or wheel
# -----------------------------------------------
# - source: use libStorageDirect.so from bin/ and the source tree's
#   py_dacs_client/ directly. Fast dev iteration.
# - wheel:  install the built wheel and run sample modules from the installed
#   package. This is what the CI pipeline uses.
DACS_TEST_MODE="${DACS_TEST_MODE:-source}"
echo "DACS test mode: $DACS_TEST_MODE"

if [ "$DACS_TEST_MODE" = "wheel" ]; then
    DACS_DIST_DIR="${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client/dist"
    WHEEL_PATH="${DACS_WHEEL:-}"
    if [ -z "$WHEEL_PATH" ] && [ -d "$DACS_DIST_DIR" ]; then
        WHEEL_PATH=$(ls -1 "$DACS_DIST_DIR"/*manylinux*.whl 2>/dev/null | head -1)
        if [ -z "$WHEEL_PATH" ]; then
            WHEEL_PATH=$(ls -1 "$DACS_DIST_DIR"/*.whl 2>/dev/null | head -1)
        fi
    fi
    if [ -z "$WHEEL_PATH" ] || [ ! -f "$WHEEL_PATH" ]; then
        echo "ERROR: DACS_TEST_MODE=wheel but no dacs-client wheel found in $DACS_DIST_DIR."
        echo "Build one first via .pipelines/scripts/build/build-python-package.sh"
        echo "or set DACS_WHEEL=/path/to/wheel.whl"
        exit 1
    fi
    # Drop stale source-tree libStorageDirect.so so the wheel's copy wins.
    rm -f ./libStorageDirect.so

    # Install into an ephemeral venv so the persistent CI runner stays clean.
    DACS_WHEEL_VENV="$(mktemp -d -t dacs-wheel-venv-XXXXXX)"
    trap 'rm -rf "$DACS_WHEEL_VENV"' EXIT
    echo "Creating ephemeral venv: $DACS_WHEEL_VENV"
    python3 -m venv "$DACS_WHEEL_VENV"
    # shellcheck disable=SC1091
    source "$DACS_WHEEL_VENV/bin/activate"
    pip install --upgrade pip wheel >/dev/null

    echo "Installing dacs-client wheel: $WHEEL_PATH"
    # Install with the [test] extra so pytest + azure-storage-blob + blobfile
    # (declared in pyproject.toml) come along automatically. The hard
    # azure-identity runtime dep is pulled in by the wheel itself.
    pip install --force-reinstall "${WHEEL_PATH}[test]"
    echo "Installed package location:"
    python3 -c "import py_dacs_client, os; print('  py_dacs_client ->', os.path.dirname(py_dacs_client.__file__))"
else
    if [ "$DACS_TEST_MODE" != "source" ]; then
        echo "WARNING: unknown DACS_TEST_MODE=$DACS_TEST_MODE, falling back to 'source'"
    fi
    # copy the intercept library to package directory
    arch_dir_name=$([[ "${BUILD_ARCH}" == *"x64"* ]] || [[ "${BUILD_ARCH}" == "amd64" ]] && echo "amd64" || echo "arm64")
    cp -v $REPOSITORY_ROOT/bin/$BUILD_CONFIGURATION/${arch_dir_name}/native-storage/storage/libStorageDirect.so ./libStorageDirect.so

    # Set up PYTHONPATH so Python can find the py_dacs_client module from the source tree.
    export PYTHONPATH="${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client:${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client/py_dacs_client:${PYTHONPATH}"
    echo "PYTHONPATH set to: $PYTHONPATH"
fi

# Enable SAS token if provided
if [[ -n "${STORAGE_INTCPT_SAS_TOKEN}" ]]; then
    export USE_SAS_TOKEN="true"
fi

# start the cache server
start_cache_server $REPOSITORY_ROOT/out_test_py_client_cache_server.log

# Generate unique datetime string for blob naming to avoid overwrite errors
DATETIME_SUFFIX=$(generate_datetime_suffix)

echo "========================================="
echo "Test 1: Static Cache Server List"
echo "========================================="

python3 -m py_dacs_client.dacs_client_test --nBlobs 10 --blobSizeToBeRead 33554432 --blobNamePattern "${DATETIME_SUFFIX}"

echo "========================================="
echo "Test 2: Cache Server Discovery API"
echo "========================================="

# Generate new datetime suffix to avoid blob name collision with Test 1
DATETIME_SUFFIX=$(generate_datetime_suffix)

# Setup discovery API config files
sudo mkdir -p /serverConfig
printf "1" | sudo tee /serverConfig/cache_server_count > /dev/null
printf "127.0.{}.1" | sudo tee /serverConfig/cache_server_endpoint_format > /dev/null

# Export discovery environment variables
export CACHE_DISCOVERY_URL="127.0.0.1"

# Unset static server list to force discovery usage
unset CACHE_SERVER_LIST

python3 -m py_dacs_client.dacs_client_test --nBlobs 10 --blobSizeToBeRead 33554432 --blobNamePattern "${DATETIME_SUFFIX}"

# Cleanup discovery env vars
unset CACHE_DISCOVERY_URL

# Restore static server list for cleanup
export CACHE_SERVER_LIST="127.0.0.1"

# cleanup
stop_cache_server
# Clean up the source-tree symlink/copy (only present in source mode)
rm -f ./libStorageDirect.so

# go back to original directory
cd $ORIGNAL_DIR
