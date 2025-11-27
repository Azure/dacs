#!/bin/bash
# Copy compiled binaries to Docker build context directory
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_CONFIGURATION="${BUILD_CONFIGURATION:-Debug}"

DOCKER_STORAGE="$REPOSITORY_ROOT/Docker/storage"
CACHE_TEST="$DOCKER_STORAGE/cache-test"
BINARIES_PATH="$REPOSITORY_ROOT/bin/$BUILD_CONFIGURATION/native-storage/storage"
OBJ_PATH="$REPOSITORY_ROOT/obj/$BUILD_CONFIGURATION/native/linux-x64/publish-storage/storage"
STRESS_TESTS_PATH="$REPOSITORY_ROOT/src/subsystems/storage/stress_tests"

echo "Build configuration: $BUILD_CONFIGURATION"
echo "Copying binaries from: $BINARIES_PATH"
echo "Alternative path: $OBJ_PATH"
echo "Destination: $DOCKER_STORAGE"

# Detect binary location (try primary and fallback paths)
FOUND_BINARIES=""
if [ -d "$BINARIES_PATH" ] && [ -f "$BINARIES_PATH/CacheServer" ]; then
  FOUND_BINARIES="$BINARIES_PATH"
elif [ -d "$OBJ_PATH" ] && [ -f "$OBJ_PATH/CacheServer" ]; then
  FOUND_BINARIES="$OBJ_PATH"
  echo "Using alternative binary path: $OBJ_PATH"
else
  echo "ERROR: Binaries not found in either location:"
  echo "  Primary: $BINARIES_PATH"
  echo "  Alternative: $OBJ_PATH"
  echo "Available files in bin directory:"
  ls -la "$REPOSITORY_ROOT/bin/$BUILD_CONFIGURATION/" 2>/dev/null || echo "  Directory does not exist"
  echo "Available files in obj directory:"
  ls -la "$REPOSITORY_ROOT/obj/$BUILD_CONFIGURATION/" 2>/dev/null || echo "  Directory does not exist"
  exit 1
fi

# Set up Docker build directories
mkdir -p "$DOCKER_STORAGE" "$CACHE_TEST"

# Copy server and client binaries to Docker context
echo "Copying binaries from: $FOUND_BINARIES"
cp "$FOUND_BINARIES/CacheServer" "$DOCKER_STORAGE/CacheServer"
cp "$FOUND_BINARIES/LivenessProbe" "$DOCKER_STORAGE/LivenessProbe"
cp "$FOUND_BINARIES/CacheStorageClient" "$CACHE_TEST/CacheStorageClient"
cp "$FOUND_BINARIES/DeleteClient" "$CACHE_TEST/DeleteClient"
cp "$FOUND_BINARIES/PerfClient" "$CACHE_TEST/PerfClient"
cp "$OBJ_PATH/libStorageIntercept.so" "$DOCKER_STORAGE/libStorageIntercept.so"

# Copy Python test scripts and configuration files
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