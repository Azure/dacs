#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_CONFIG="${1:-Debug}"

echo "Building storage ($BUILD_CONFIG)..."

cd "$REPOSITORY_ROOT"

# Step 1: Restore
./.pipelines/restore.sh "$BUILD_CONFIG"

# Step 2: Build
BUILD_STORAGE_ONLY=true ./.pipelines/build.sh "$BUILD_CONFIG"

echo "Build complete: ./bin/$BUILD_CONFIG/"
echo ""
echo "Built binaries:"
ls -lh "./bin/$BUILD_CONFIG/native-storage/storage/" 2>/dev/null || echo "No binaries found"