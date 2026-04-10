#!/bin/bash
# -----------------------------------------------
# DACS Python Unit Tests Runner
# -----------------------------------------------
# This script runs pytest for all Python unit tests in the repository.
# Usage: ./pytest.sh [BUILD_TYPE] [PYTEST_ARGS...]
#
# Examples:
#   ./pytest.sh Debug
#   ./pytest.sh Release -v -k test_read
#   ./pytest.sh Debug --tb=short
# -----------------------------------------------

set -e

BUILD_ARCH="${BUILD_ARCH:-amd64}"
BUILD_TYPE="${1:-Debug}"
shift || true  # Shift to get remaining args as pytest arguments

# Identity client ID for managed identity used in the test pipeline
# This is the user assigned identity used for the azdacs build pool
# The identity has storage blob data contributor role on storage accounts:
# - storageinterceptionpr1 
# - storageinterceptionpr2
# Reference: internal-reference-removed
# Allow user to override with USER_VM_AZURE_CLIENT_ID for local development
if [ -n "${USER_VM_AZURE_CLIENT_ID:-}" ]; then
    export AZURE_CLIENT_ID="$USER_VM_AZURE_CLIENT_ID"
    echo "Using user-provided AZURE_CLIENT_ID from USER_VM_AZURE_CLIENT_ID"
else
    export AZURE_CLIENT_ID="00000000-0000-0000-0000-000000000000"
    echo "Using pipeline default AZURE_CLIENT_ID"
fi

# Enable debug logging for DACS client
export DACS_DATA_LOG_LEVEL="DEBUG"

# SAS can be used for tests for local development
if [ "${USE_SAS_TOKEN:-false}" = "true" ] && [ -n "${STORAGE_INTCPT_SAS_TOKEN:-}" ]; then
    echo "SAS token is set for authentication (USE_SAS_TOKEN=true)"
fi


SCRIPT_DIR="$(dirname "$(realpath "$0")")"
REPOSITORY_ROOT=$(realpath "$SCRIPT_DIR/..")

echo "========================================"
echo "DACS Python Unit Tests"
echo "========================================"
echo "Build Type: $BUILD_TYPE"
echo "Build Arch: $BUILD_ARCH"
echo "Repository: $REPOSITORY_ROOT"
echo "========================================"

# Activate virtual environment if available
if [ -d "/venv" ]; then
    echo "Activating virtual environment..."
    source /venv/bin/activate
fi

# Ensure pytest is installed
if ! command -v pytest &> /dev/null; then
    echo "Installing pytest..."
    pip install pytest pytest-timeout pytest-asyncio
else
    # Ensure pytest-timeout is installed
    if ! pip show pytest-timeout &> /dev/null 2>&1; then
        echo "Installing pytest-timeout..."
        pip install pytest-timeout
    fi
    # Ensure pytest-asyncio is installed
    if ! pip show pytest-asyncio &> /dev/null 2>&1; then
        echo "Installing pytest-asyncio..."
        pip install pytest-asyncio
    fi
fi

# Install Azure SDK for test blob cleanup (used by conftest.py pytest_sessionfinish)
# These packages are needed to delete test blobs after tests complete
if ! pip show azure-storage-blob &> /dev/null 2>&1; then
    echo "Installing Azure SDK for test cleanup..."
    pip install azure-storage-blob azure-identity
fi

# Install blobfile for patcher tests
if ! pip show blobfile &> /dev/null 2>&1; then
    echo "Installing blobfile for patcher tests..."
    pip install blobfile
fi

# Set up environment for tests
# Add both the py_dacs_client directory (for direct imports) and its parent (for package imports)
export PYTHONPATH="${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client:${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client/py_dacs_client:${PYTHONPATH}"

# Set up library paths for native binaries
NATIVE_LIB_PATH="${REPOSITORY_ROOT}/bin/${BUILD_TYPE}/${BUILD_ARCH}/native-storage/storage"
echo "Native Lib Path: $NATIVE_LIB_PATH"
if [ -d "$NATIVE_LIB_PATH" ]; then
    export LD_LIBRARY_PATH="${NATIVE_LIB_PATH}:${LD_LIBRARY_PATH}"
    echo "Added native library path: $NATIVE_LIB_PATH"
    
    # Create symlinks for native libraries in the py_dacs_client directory
    # The Python module expects the libraries in its own directory
    PY_CLIENT_DIR="${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client/py_dacs_client"
    
    # Symlink libStorageDirect.so
    LIB_DIRECT_SOURCE="${NATIVE_LIB_PATH}/libStorageDirect.so"
    LIB_DIRECT_TARGET="${PY_CLIENT_DIR}/libStorageDirect.so"
    
    if [ -f "$LIB_DIRECT_SOURCE" ] && [ ! -f "$LIB_DIRECT_TARGET" ]; then
        echo "Creating symlink for libStorageDirect.so..."
        ln -sf "$LIB_DIRECT_SOURCE" "$LIB_DIRECT_TARGET"
        echo "Symlinked: $LIB_DIRECT_TARGET -> $LIB_DIRECT_SOURCE"
    elif [ -f "$LIB_DIRECT_TARGET" ]; then
        echo "libStorageDirect.so already exists at $LIB_DIRECT_TARGET"
    else
        echo "WARNING: libStorageDirect.so not found at $LIB_DIRECT_SOURCE"
    fi
fi

# Define test directories
TEST_DIRS=(
    "${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client/py_dacs_client/tests"
)

# Directories to exclude from test collection
EXCLUDE_DIRS=(
    "blobfile_base_tests"
)

# Collect all existing test directories
EXISTING_TEST_DIRS=()
for dir in "${TEST_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        EXISTING_TEST_DIRS+=("$dir")
        echo "Found test directory: $dir"
    fi
done

if [ ${#EXISTING_TEST_DIRS[@]} -eq 0 ]; then
    echo "No test directories found!"
    exit 1
fi

echo ""
echo "Running pytest..."
echo "----------------------------------------"

# Build ignore options for excluded directories
IGNORE_OPTS=""
for exclude_dir in "${EXCLUDE_DIRS[@]}"; do
    IGNORE_OPTS="$IGNORE_OPTS --ignore-glob=*/${exclude_dir}/*"
done

# Debug: Print environment and library information
echo ""
echo "=== Debug Information ==="
echo "Python version: $(python3 --version 2>&1)"
echo "Pytest version: $(pytest --version 2>&1)"
echo "LD_LIBRARY_PATH: ${LD_LIBRARY_PATH:-not set}"
echo "PYTHONPATH: ${PYTHONPATH:-not set}"
echo ""

# Check if native libraries exist and are valid
PY_CLIENT_DIR="${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client/py_dacs_client"
echo "=== Native Library Check ==="

if [ -f "${PY_CLIENT_DIR}/libStorageDirect.so" ]; then
    echo "libStorageDirect.so exists"
    ls -la "${PY_CLIENT_DIR}/libStorageDirect.so"
    file "${PY_CLIENT_DIR}/libStorageDirect.so" 2>&1 || echo "file command failed"
    ldd "${PY_CLIENT_DIR}/libStorageDirect.so" 2>&1 || echo "ldd command failed"
else
    echo "WARNING: libStorageDirect.so NOT FOUND at ${PY_CLIENT_DIR}"
fi
echo ""

# Check config file
echo "=== Storage Intercept Config ==="
if [ -f "/tmp/storageIntercept.config" ]; then
    echo "Config file exists at /tmp/storageIntercept.config"
    cat /tmp/storageIntercept.config
else
    echo "Config file NOT FOUND at /tmp/storageIntercept.config"
fi
echo ""

# If a federated token was passed as an env var (e.g. from the pipeline), write it to a temp
# file inside this process/container and export AZURE_FEDERATED_TOKEN_FILE so that the Azure
# Identity SDK EnvironmentCredential can pick it up.  The file must exist in the same
# filesystem as the process that reads it, which is why we create it here rather than on the
# host before invoking bldenv.sh / Docker.
if [ -n "${AZURE_FEDERATED_TOKEN:-}" ] && [ -z "${AZURE_FEDERATED_TOKEN_FILE:-}" ]; then
    _FEDERATED_TOKEN_FILE=$(mktemp)
    echo "$AZURE_FEDERATED_TOKEN" > "$_FEDERATED_TOKEN_FILE"
    export AZURE_FEDERATED_TOKEN_FILE="$_FEDERATED_TOKEN_FILE"
    echo "Federated token written to $AZURE_FEDERATED_TOKEN_FILE"
fi

# Enable core dumps for debugging
ulimit -c unlimited 2>/dev/null || echo "Could not set ulimit for core dumps"
echo "Core dump size limit: $(ulimit -c 2>/dev/null || echo 'unknown')"
echo ""

# Enable Python faulthandler + allocator diagnostics for native crash debugging
export PYTHONFAULTHANDLER=1
export PYTHONMALLOC=debug  # Detects heap misuse (buffer overflows, use-after-free, etc.)

# Run pytest with collected test directories
# -X faulthandler: Enable faulthandler for native crash tracebacks
# --timeout=300: 5 minute timeout per test (for integration tests that may be slow)
# -vv: very verbose output
# -x: stop on first failure
# -s: no capture, show stdout/stderr immediately
# --tb=long: longer traceback format for debugging
# --junitxml: generate JUnit XML report for CI/CD integration
# $@: pass through any additional arguments
echo "=== Running pytest command ==="
echo "python -X faulthandler -m pytest --timeout=300 -vv -x -s --tb=long --junitxml=${REPOSITORY_ROOT}/pytest-results.xml $IGNORE_OPTS ${EXISTING_TEST_DIRS[@]} $@"
echo ""

# Run pytest with faulthandler enabled via -X flag
python -X faulthandler -m pytest \
    --timeout=300 \
    -vv \
    -s \
    --tb=long \
    --junitxml="${REPOSITORY_ROOT}/pytest-results.xml" \
    $IGNORE_OPTS \
    "${EXISTING_TEST_DIRS[@]}" \
    "$@"

PYTEST_EXIT_CODE=$?

# Check for core dumps
echo ""
echo "=== Core Dump Check ==="
if ls core* 2>/dev/null; then
    echo "Core dump files found:"
    ls -la core* 2>/dev/null
    # Try to get backtrace if gdb is available
    if command -v gdb &> /dev/null; then
        echo "Attempting to get backtrace from core dump..."
        CORE_FILE=$(ls core* 2>/dev/null | head -1)
        if [ -n "$CORE_FILE" ]; then
            gdb -batch -ex "bt full" -ex "quit" python3 "$CORE_FILE" 2>&1 || echo "GDB backtrace failed"
        fi
    fi
else
    echo "No core dump files found in current directory"
fi

# Also check /tmp for core dumps
if ls /tmp/core* 2>/dev/null; then
    echo "Core dump files found in /tmp:"
    ls -la /tmp/core* 2>/dev/null
fi

echo ""
echo "========================================"
if [ $PYTEST_EXIT_CODE -eq 0 ]; then
    echo "All Python tests passed!"
else
    echo "Python tests failed (exit code: $PYTEST_EXIT_CODE)"
fi
echo "========================================"

exit $PYTEST_EXIT_CODE
