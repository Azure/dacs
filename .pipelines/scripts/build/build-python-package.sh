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

# Script to build Python package (build only, no publishing)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
readonly VERSION_HELPER="$REPOSITORY_ROOT/.pipelines/scripts/get-version.sh"
PYTHON_PACKAGE_VERSION="$("$VERSION_HELPER" python)"
readonly PYTHON_PACKAGE_VERSION

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

readonly INIT_FILE="py_dacs_client/__init__.py"
readonly PACKAGE_LICENSE="LICENSE"
readonly PACKAGE_NOTICE="NOTICE.txt"
STAGED_COMPLIANCE_FILES=()
BACKUP_DIR="$(mktemp -d ".dacs-python-package.XXXXXX")"
readonly BACKUP_DIR

cleanup_package_build() {
  local staged_file
  for staged_file in "${STAGED_COMPLIANCE_FILES[@]}"; do
    rm -f -- "$staged_file"
  done
  cp -- "$BACKUP_DIR/__init__.py" "$INIT_FILE"
  rm -rf -- "$BACKUP_DIR"
}

cp -- "$INIT_FILE" "$BACKUP_DIR/__init__.py"
trap cleanup_package_build EXIT

for source_file in "$REPOSITORY_ROOT/LICENSE" "$REPOSITORY_ROOT/NOTICE.txt"; do
  if [[ ! -f "$source_file" ]]; then
    echo "Error: Required compliance file not found: $source_file" >&2
    exit 1
  fi
done

if [[ -e "$PACKAGE_LICENSE" || -e "$PACKAGE_NOTICE" ]]; then
  echo "Error: Refusing to overwrite staged package compliance files" >&2
  exit 1
fi

cp -- "$REPOSITORY_ROOT/LICENSE" "$PACKAGE_LICENSE"
STAGED_COMPLIANCE_FILES+=("$PACKAGE_LICENSE")
cp -- "$REPOSITORY_ROOT/NOTICE.txt" "$PACKAGE_NOTICE"
STAGED_COMPLIANCE_FILES+=("$PACKAGE_NOTICE")

echo ""
echo "Python package version: $PYTHON_PACKAGE_VERSION"
echo ""

# Extract placeholder version from __init__.py (should be '0.9.0-PLACEHOLDER')
ORIGINAL_VERSION=$(grep "^__version__ = " "$INIT_FILE" | cut -d '"' -f 2)

if [ "$ORIGINAL_VERSION" != "0.9.0-PLACEHOLDER" ]; then
  echo "Warning: Expected __version__ = \"0.9.0-PLACEHOLDER\" in $INIT_FILE, found: $ORIGINAL_VERSION"
fi

# Replace placeholder with actual calculated version
sed -i "s/__version__ = \"${ORIGINAL_VERSION}\"/__version__ = \"${PYTHON_PACKAGE_VERSION}\"/" "$INIT_FILE"

echo "Version updated in $INIT_FILE: $ORIGINAL_VERSION -> $PYTHON_PACKAGE_VERSION"
echo ""

# Clean previous builds
rm -rf -- dist/ build/ ./*.egg-info

# Ensure pip is available
if ! $PYTHON_CMD -m pip --version &> /dev/null; then
  echo "Installing pip..."
  $PYTHON_CMD -m ensurepip --default-pip 2>/dev/null || { sudo apt-get update && sudo apt-get install -y python3-pip; }
fi

# Install build dependencies. patchelf is pulled in as a pip wheel (it ships a
# prebuilt binary) so auditwheel can repair the wheel without a system package
# manager or sudo - the native-build container runs as a non-root user.
echo "Installing build dependencies..."
$PYTHON_CMD -m pip install --upgrade build pip setuptools wheel packaging "auditwheel==6.7.0" patchelf -q

# pip may install the patchelf/auditwheel entry points under the user site
# (~/.local/bin) when running as a non-root container user; make sure auditwheel
# can find the patchelf binary on PATH.
USER_BASE="$($PYTHON_CMD -c 'import site; print(site.getuserbase())')"
readonly USER_BASE
export PATH="${USER_BASE}/bin:$PATH"

# Export architecture for setup.py to use
export BUILD_ARCHITECTURE="$BUILD_ARCH"

# Build wheel
echo "Building Python wheel..."
set +e
$PYTHON_CMD -m build --wheel --no-isolation
BUILD_EXIT_CODE=$?
set -e

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
echo "----------------------------------------------"
echo "Running auditwheel repair"
echo "----------------------------------------------"

# Find the single wheel produced by build
WHEEL_COUNT=$(find dist/ -maxdepth 1 -name '*.whl' | wc -l)
if [ "$WHEEL_COUNT" -ne 1 ]; then
  echo "Error: Expected exactly 1 wheel in dist/, found $WHEEL_COUNT"
  ls -1 dist/*.whl 2>/dev/null
  exit 1
fi
ORIGINAL_WHEEL=$(find dist/ -maxdepth 1 -name '*.whl' -print -quit)
echo "Wheel: $ORIGINAL_WHEEL"
echo ""

# Show what manylinux policy the wheel qualifies for
echo "auditwheel show:"
$PYTHON_CMD -m auditwheel show "$ORIGINAL_WHEEL" || true
echo ""

echo "Repairing wheel..."
$PYTHON_CMD -m auditwheel repair "$ORIGINAL_WHEEL" -w dist/

# auditwheel creates a new wheel with manylinux tag alongside the original.
# Remove the original linux_* wheel so only the repaired one is published.
if [ -f "$ORIGINAL_WHEEL" ] && echo "$ORIGINAL_WHEEL" | grep -qv "manylinux"; then
  REPAIRED_WHEEL=$(find dist/ -maxdepth 1 -name '*manylinux*.whl' -print -quit)
  if [ -n "$REPAIRED_WHEEL" ]; then
    echo "Removing unrepaired wheel: $(basename "$ORIGINAL_WHEEL")"
    rm "$ORIGINAL_WHEEL"
  fi
fi

echo ""
echo "auditwheel repair complete. Repaired wheel:"
ls -1 dist/*.whl | sed 's/^/  /'

echo ""
echo "=============================================="
echo "Build Complete!"
echo "=============================================="
echo "Architecture: $BUILD_ARCH"
echo "Generated wheel(s):"
ls -1 dist/*.whl | sed 's/^/  - /'
echo ""
echo "Package details:"
ls -lh dist/ | tail -n +2 | awk '{print "  "$9" ("$5")"}'
echo "=============================================="

echo ""
echo "Verifying repaired wheel(s) with auditwheel show:"
for whl in dist/*.whl; do
  echo "--- $(basename "$whl") ---"
  $PYTHON_CMD -m auditwheel show "$whl"
  $PYTHON_CMD - "$whl" <<'PY'
import pathlib
import sys
import zipfile

wheel = pathlib.Path(sys.argv[1])
required = {"LICENSE", "NOTICE.txt"}
with zipfile.ZipFile(wheel) as archive:
    packaged = {pathlib.PurePosixPath(name).name for name in archive.namelist()}

missing = sorted(required - packaged)
if missing:
    raise SystemExit(f"{wheel} is missing compliance files: {', '.join(missing)}")
PY
done

exit 0
