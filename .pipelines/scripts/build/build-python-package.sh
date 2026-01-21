#!/bin/bash
# Script to build Python package (build only, no publishing)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# Get architecture from environment (set by pipeline)
BUILD_ARCH="${BUILD_ARCHITECTURE:-$(uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')}"

echo "=============================================="
echo "Building Python Package"
echo "=============================================="
echo "Architecture: $BUILD_ARCH"
echo "Repository: $REPOSITORY_ROOT"
echo ""

# Check if python is available
if ! command -v python3 &> /dev/null && ! command -v python &> /dev/null; then
  echo "Python not found, installing..."
  if command -v apt-get &> /dev/null; then
    sudo apt-get update
    sudo apt-get install -y python3 python3-pip
  elif command -v yum &> /dev/null; then
    sudo yum install -y python3 python3-pip
  else
    echo "Warning: Could not install Python (unsupported package manager)"
  fi
fi

# Set Python command (prefer python3)
if command -v python3 &> /dev/null; then
  PYTHON_CMD=python3
elif command -v python &> /dev/null; then
  PYTHON_CMD=python
else
  echo "Error: Python installation failed"
  exit 1
fi

cd "$REPOSITORY_ROOT/src/subsystems/storage/dacs_client"

# =============================================================================
# Version and channel - provided by pipeline via environment variables
# =============================================================================
if [ -z "$PYTHON_PACKAGE_VERSION" ] || [ -z "$PACKAGE_CHANNEL" ]; then
  echo "ERROR: PYTHON_PACKAGE_VERSION and PACKAGE_CHANNEL must be set!"
  echo "  PYTHON_PACKAGE_VERSION=${PYTHON_PACKAGE_VERSION:-<not set>}"
  echo "  PACKAGE_CHANNEL=${PACKAGE_CHANNEL:-<not set>}"
  exit 1
fi

echo ""
echo "Python package version: $PYTHON_PACKAGE_VERSION"
echo "Python package channel: $PACKAGE_CHANNEL"
echo ""

# Update VERSION in setup.py with the calculated version
SETUP_FILE="setup.py"

# Extract placeholder version from setup.py (should be '0.9.0-PLACEHOLDER')
ORIGINAL_VERSION=$(grep "^VERSION = " "$SETUP_FILE" | cut -d "'" -f 2)

if [ "$ORIGINAL_VERSION" != "0.9.0-PLACEHOLDER" ]; then
  echo "Warning: Expected VERSION = '0.9.0-PLACEHOLDER' in setup.py, found: $ORIGINAL_VERSION"
fi

# Replace placeholder with actual calculated version
sed "s/VERSION = '${ORIGINAL_VERSION}'/VERSION = '${PYTHON_PACKAGE_VERSION}'/" "$SETUP_FILE" > "${SETUP_FILE}.tmp"
mv "${SETUP_FILE}.tmp" "$SETUP_FILE"

echo "Version updated in setup.py: $ORIGINAL_VERSION → $PYTHON_PACKAGE_VERSION"
echo ""

# Clean previous builds
rm -rf dist/ build/ *.egg-info

# Ensure pip is available
if ! $PYTHON_CMD -m pip --version &> /dev/null; then
  echo "Installing pip..."
  $PYTHON_CMD -m ensurepip --default-pip 2>/dev/null || sudo apt-get install -y python3-pip
fi

# Install build dependencies
echo "Installing build dependencies..."
$PYTHON_CMD -m pip install --upgrade pip setuptools wheel packaging -q

# Export architecture and channel for setup.py to use
export BUILD_ARCHITECTURE="$BUILD_ARCH"
export BUILD_CHANNEL="$PACKAGE_CHANNEL"

# Build wheel
echo "Building Python wheel..."
$PYTHON_CMD setup.py bdist_wheel
BUILD_EXIT_CODE=$?

# Restore original setup.py
if ! git checkout setup.py 2>/dev/null; then
  echo "Warning: Could not restore setup.py from git. You may need to revert manually."
fi

if [ $BUILD_EXIT_CODE -ne 0 ]; then
  echo "Error: Failed to build Python package"
  exit 1
fi

# Verify wheel was created
if [ ! -d "dist" ] || [ -z "$(ls -A dist/*.whl 2>/dev/null)" ]; then
  echo "Error: No wheel files found in dist/ directory"
  exit 1
fi

echo ""
echo "=============================================="
echo "✅ Build Complete!"
echo "=============================================="
echo "Architecture: $BUILD_ARCH"
echo "Generated wheel(s):"
ls -1 dist/*.whl | sed 's/^/  - /'
echo ""
echo "Package details:"
ls -lh dist/ | tail -n +2 | awk '{print "  "$9" ("$5")"}'
echo "=============================================="

exit 0
