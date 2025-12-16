#!/bin/bash
# Build storage components using CMake
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_CONFIG="${1:-Debug}"
BUILD_ARCH="${2:-${BUILD_ARCH:-amd64}}"

echo "Building storage ($BUILD_CONFIG, $BUILD_ARCH)..."

cd "$REPOSITORY_ROOT"

# Set up build directory
BUILD_DIR="$REPOSITORY_ROOT/build/${BUILD_CONFIG}/${BUILD_ARCH}"
mkdir -p "$BUILD_DIR"

# Get git commit hash
GIT_COMMIT_HASH=$(git rev-parse HEAD 2>/dev/null || echo "unknown_git_commit")

# Set CMake output directories (CMakeLists append "-storage/storage" inside targets)
export CMAKE_RUNTIME_OUTPUT_DIRECTORY="$REPOSITORY_ROOT/bin/${BUILD_CONFIG}/${BUILD_ARCH}/native"
export CMAKE_LIBRARY_OUTPUT_DIRECTORY="$REPOSITORY_ROOT/bin/${BUILD_CONFIG}/${BUILD_ARCH}/native"
export CMAKE_REMOTE_PUBLISH_DIRECTORY="$REPOSITORY_ROOT/bin/${BUILD_CONFIG}/${BUILD_ARCH}/native"
export CMAKE_DEP_INSTALL_DIR="/dependency"
export BUILD_ARCH="$BUILD_ARCH"

# Enable ccache for faster rebuilds
export CCACHE_DIR="$REPOSITORY_ROOT/.ccache"
mkdir -p "$CCACHE_DIR"

# Configure with CMake
cd "$BUILD_DIR"
cmake \
  -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
  -DCMAKE_TOOLCHAIN_FILE="$REPOSITORY_ROOT/toolchain.cmake" \
  -DGIT_COMMIT_HASH="$GIT_COMMIT_HASH" \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -G Ninja \
  "$REPOSITORY_ROOT"

# Build with Ninja - use all available cores
ninja -j$(nproc)

echo ""
echo "Build complete!"
echo ""
echo "Binaries location: $REPOSITORY_ROOT/bin/${BUILD_CONFIG}/${BUILD_ARCH}/native-storage/storage"