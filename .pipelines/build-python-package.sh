#!/bin/bash

# Script to build and optionally publish Python package
# Usage: ./build-python-package.sh [BUILD_ARCHITECTURE] [--publish]
# BUILD_ARCHITECTURE: linux-x64 (default) or linux-arm64

BUILD_ARCH="${BUILD_ARCHITECTURE:-linux-x64}"
PUBLISH_FLAG=false

# Parse arguments
for arg in "$@"; do
  if [[ "$arg" == "--publish" ]]; then
    PUBLISH_FLAG=true
  elif [[ "$arg" =~ ^linux- ]]; then
    BUILD_ARCH="$arg"
  fi
done

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "Building Python package for architecture: $BUILD_ARCH"

echo "Building Python package..."

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
  echo "Warning: Python installation failed, skipping Python package build"
  exit 0
fi

cd $DIR/../src/subsystems/storage/dacs_client

# Get build number from environment or use timestamp
if [ ! -z "$BUILD_BUILDNUMBER" ]; then
  # Azure DevOps build number format: use the full build number and remove dots for uniqueness
  # Example: 20251229.9 -> 202512299
  BUILD_TAG=$(echo "$BUILD_BUILDNUMBER" | tr -d '.')
elif [ ! -z "$PACKAGE_REVISION" ]; then
  BUILD_TAG=$PACKAGE_REVISION
else
  # Use timestamp as fallback for local builds
  BUILD_TAG=$(date +%Y%m%d%H%M%S)
fi

echo "Build tag: $BUILD_TAG"

# While updating version, ensure to update in dacs_client/setup.py as well.
BASE_VERSION="0.9.0"
# Update VERSION in setup.py to append build tag (e.g., 1.0.0.12345678)
SETUP_FILE="setup.py"

# Create a temporary setup.py with updated version
sed "s/VERSION = '${BASE_VERSION}'/VERSION = '${BASE_VERSION}.${BUILD_TAG}'/" $SETUP_FILE > ${SETUP_FILE}.tmp
mv ${SETUP_FILE}.tmp $SETUP_FILE

# Clean previous builds
rm -rf dist/ build/bdist* *.egg-info

# Ensure pip is available
$PYTHON_CMD -m pip --version &> /dev/null || $PYTHON_CMD -m ensurepip --default-pip

# Install build dependencies
$PYTHON_CMD -m pip install --upgrade pip setuptools wheel

# Export architecture for setup.py to use
export BUILD_ARCHITECTURE="$BUILD_ARCH"

# Build wheel
$PYTHON_CMD setup.py bdist_wheel
BUILD_EXIT_CODE=$?

# Restore original setup.py (revert version change)
git checkout setup.py 2>/dev/null || echo "Note: Could not restore setup.py from git"

if [ $BUILD_EXIT_CODE -ne 0 ]; then
  echo "Error: Failed to build Python package"
  exit 1
fi

# Verify dist directory and wheel files exist
if [ ! -d "dist" ] || [ -z "$(ls -A dist/*.whl 2>/dev/null)" ]; then
  echo "Error: No wheel files found in dist/ directory"
  exit 1
fi

echo "Python package built successfully for $BUILD_ARCH"
echo "Generated wheel file(s):"
ls -1 dist/*.whl
ls -la dist/

# Publish to Azure Artifacts feed (if requested and PAT token is available)
if [ "$PUBLISH_FLAG" = true ]; then
  if [ ! -z "$AZURE_DEVOPS_EXT_PAT" ]; then
    echo "Publishing Python package to Azure Artifacts..."
    
    # Install twine if not available
    $PYTHON_CMD -m pip install --upgrade twine
    
    # Publish to feed (without --skip-existing as Azure Artifacts doesn't support it)
    $PYTHON_CMD -m twine upload \
      --repository-url internal-reference-removed \
      --username "internal" \
      --password "$AZURE_DEVOPS_EXT_PAT" \
      dist/*.whl
    
    if [ $? -eq 0 ]; then
      echo "Python package published successfully"
    else
      echo "Warning: Failed to publish Python package (exit code $?)"
      exit 1
    fi
  else
    echo "Skipping Python package publish (AZURE_DEVOPS_EXT_PAT not set)"
  fi
fi

exit 0
