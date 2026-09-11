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
export CCACHE_BASEDIR="$REPOSITORY_ROOT"
export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-2G}"
mkdir -p "$CCACHE_DIR"
ccache --zero-stats

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
ninja -j"$(nproc)"
ccache --show-stats

# Build Rust client
echo "Building Rust client..."
cargo_flags=(--locked --manifest-path "$REPOSITORY_ROOT/src/subsystems/storage/rust/dacs_client/Cargo.toml")
if [[ "${BUILD_CONFIG}" == "Release" ]]; then cargo_flags=(--release "${cargo_flags[@]}"); fi
cargo build "${cargo_flags[@]}"

echo ""
echo "Build complete!"
echo ""
echo "Binaries location: $REPOSITORY_ROOT/bin/${BUILD_CONFIG}/${BUILD_ARCH}/native-storage/storage"