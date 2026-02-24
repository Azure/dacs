#!/bin/bash
# Copy compiled binaries to Docker build context directory
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_CONFIGURATION="${BUILD_CONFIGURATION:-Debug}"
BUILD_ARCH="${BUILD_ARCH:-amd64}"

DOCKER_STORAGE="$REPOSITORY_ROOT/Docker/storage"
CACHE_TEST="$DOCKER_STORAGE/cache-test"
BINARIES_PATH="$REPOSITORY_ROOT/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/native-storage/storage"
STRESS_TESTS_PATH="$REPOSITORY_ROOT/src/subsystems/storage/stress_tests"
PY_DACS_CLIENT_PATH="$REPOSITORY_ROOT/src/subsystems/storage/dacs_client/py_dacs_client"
PY_DACS_CLIENT="$DOCKER_STORAGE/py_dacs_client"

echo "Build configuration: $BUILD_CONFIGURATION"
echo "Build architecture: $BUILD_ARCH"
echo "Copying binaries from: $BINARIES_PATH"
echo "Destination: $DOCKER_STORAGE"

# Verify binaries exist
if [ ! -d "$BINARIES_PATH" ] || [ ! -f "$BINARIES_PATH/CacheServer" ]; then
  echo "ERROR: Binaries not found at $BINARIES_PATH"
  echo "Available files in bin directory:"
  ls -la "$REPOSITORY_ROOT/bin/${BUILD_CONFIGURATION}/${BUILD_ARCH}/" 2>/dev/null || echo "  Directory does not exist"
  exit 1
fi

# Set up Docker build directories
mkdir -p "$DOCKER_STORAGE" "$CACHE_TEST" "$PY_DACS_CLIENT"

# Ensure write permissions for current user (fixes root-owned files from containers)
chmod -R u+w "$DOCKER_STORAGE" 2>/dev/null || true

# Copy server and client binaries to Docker context
echo "Copying binaries from: $BINARIES_PATH"
cp "$BINARIES_PATH/CacheServer" "$DOCKER_STORAGE/CacheServer"
cp "$BINARIES_PATH/LivenessProbe" "$DOCKER_STORAGE/LivenessProbe"
cp "$BINARIES_PATH/CacheStorageClient" "$CACHE_TEST/CacheStorageClient"
cp "$BINARIES_PATH/DeleteClient" "$CACHE_TEST/DeleteClient"
cp "$BINARIES_PATH/PerfClient" "$CACHE_TEST/PerfClient"
cp "$BINARIES_PATH/libStorageIntercept.so" "$DOCKER_STORAGE/libStorageIntercept.so"
cp "$BINARIES_PATH/libStorageDirect.so" "$DOCKER_STORAGE/libStorageDirect.so"

# Copy Python test scripts and configuration files
cp "$STRESS_TESTS_PATH/launcher.py" "$CACHE_TEST/launcher.py"
cp "$STRESS_TESTS_PATH/StorageClient.py" "$CACHE_TEST/StorageClient.py"
cp "$STRESS_TESTS_PATH/storageIntercept.config" "$CACHE_TEST/storageIntercept.config"
cp "$BINARIES_PATH/libStorageDirect.so" "$CACHE_TEST/libStorageDirect.so"
cp "$BINARIES_PATH/dacsfs" "$CACHE_TEST/dacsfs"

# Copy py_dacs_client library (excluding tests and __pycache__)
echo "Copying py_dacs_client library..."
# Copy Python files (library only, exclude tests directory and __pycache__)
for f in "$PY_DACS_CLIENT_PATH"/*.py; do
    [ -f "$f" ] && cp "$f" "$PY_DACS_CLIENT/"
done

# Summary
echo ""
echo "Binaries copied successfully to $DOCKER_STORAGE"
echo ""
echo "Server binaries:"
ls -lh "$DOCKER_STORAGE" | grep -E "CacheServer|LivenessProbe|libStorageIntercept"
echo ""
echo "Test client binaries:"
ls -lh "$CACHE_TEST"
echo ""
echo "py_dacs_client library:"
ls -lh "$PY_DACS_CLIENT"