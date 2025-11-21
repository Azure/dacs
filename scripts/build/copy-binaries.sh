#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_CONFIG="${BUILD_TYPE:-Debug}"

DOCKER_STORAGE="$REPOSITORY_ROOT/Docker/storage"
CACHE_TEST="$DOCKER_STORAGE/cache-test"
BINARIES_PATH="$REPOSITORY_ROOT/bin/$BUILD_CONFIG/native-storage/storage"
OBJ_PATH="$REPOSITORY_ROOT/obj/$BUILD_CONFIG/native/linux-x64/publish-storage/storage"
STRESS_TESTS_PATH="$REPOSITORY_ROOT/src/subsystems/storage/stress_tests"

echo "Copying binaries from: $BINARIES_PATH"
echo "Destination: $DOCKER_STORAGE"

# Check if binaries exist
if [ ! -d "$BINARIES_PATH" ] || [ ! -f "$BINARIES_PATH/CacheServer" ]; then
  echo "ERROR: Binaries not found in $BINARIES_PATH"
  echo "Please run 'make build' first to build the binaries."
  exit 1
fi

# Create required directories
mkdir -p "$DOCKER_STORAGE" "$CACHE_TEST"

# Copy binaries
cp "$BINARIES_PATH/CacheServer" "$DOCKER_STORAGE/CacheServer"
cp "$BINARIES_PATH/LivenessProbe" "$DOCKER_STORAGE/LivenessProbe"
cp "$BINARIES_PATH/CacheStorageClient" "$CACHE_TEST/CacheStorageClient"
cp "$BINARIES_PATH/DeleteClient" "$CACHE_TEST/DeleteClient"
cp "$BINARIES_PATH/PerfClient" "$CACHE_TEST/PerfClient"
cp "$OBJ_PATH/libStorageIntercept.so" "$DOCKER_STORAGE/libStorageIntercept.so"

# Copy Python test scripts and config
cp "$STRESS_TESTS_PATH/launcher.py" "$CACHE_TEST/launcher.py"
cp "$STRESS_TESTS_PATH/StorageClient.py" "$CACHE_TEST/StorageClient.py"
cp "$STRESS_TESTS_PATH/storageIntercept.config" "$CACHE_TEST/storageIntercept.config"

# Summary
echo ""
echo "Binaries copied successfully to $DOCKER_STORAGE"
echo ""
echo "Server binaries:"
ls -lh "$DOCKER_STORAGE" | grep -E "CacheServer|LivenessProbe|libStorageIntercept"
echo ""
echo "Test client binaries:"
ls -lh "$CACHE_TEST"