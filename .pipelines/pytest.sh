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

# Identity client ID for the managed identity used in the test pipeline.
# Allow user to override with USER_VM_AZURE_CLIENT_ID for local development.
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

# -----------------------------------------------
# Test mode: source (default) or wheel
# -----------------------------------------------
# - source: PYTHONPATH points at the source tree and libStorageDirect.so is
#   symlinked from bin/. Fast iteration loop for developers - edits to Python
#   source are picked up immediately without rebuilding the wheel.
# - wheel:  installs the built wheel (from dist/ or $DACS_WHEEL) into the
#   current Python environment and runs tests against the installed package.
#   This is what the CI pipeline uses so packaging regressions (missing data
#   files, broken auditwheel repair, etc.) are caught before publish.
DACS_TEST_MODE="${DACS_TEST_MODE:-source}"
echo "DACS test mode: $DACS_TEST_MODE"

PY_CLIENT_SRC_DIR="${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client/py_dacs_client"
TEST_REQUIREMENTS="${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client/requirements-test.txt"

if [ "$DACS_TEST_MODE" = "wheel" ]; then
    # ---- wheel mode -----------------------------------------------------
    DACS_DIST_DIR="${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client/dist"
    WHEEL_PATH="${DACS_WHEEL:-}"
    if [ -z "$WHEEL_PATH" ] && [ -d "$DACS_DIST_DIR" ]; then
        WHEEL_PATH=$(ls -1 "$DACS_DIST_DIR"/*manylinux*.whl 2>/dev/null | head -1)
        if [ -z "$WHEEL_PATH" ]; then
            WHEEL_PATH=$(ls -1 "$DACS_DIST_DIR"/*.whl 2>/dev/null | head -1)
        fi
    fi
    if [ -z "$WHEEL_PATH" ] || [ ! -f "$WHEEL_PATH" ]; then
        echo "ERROR: DACS_TEST_MODE=wheel but no dacs-client wheel found."
        echo "Expected a wheel in: $DACS_DIST_DIR"
        echo "Build one first via .pipelines/scripts/build/build-python-package.sh"
        echo "or set DACS_WHEEL=/path/to/wheel.whl"
        exit 1
    fi

    # Belt-and-braces: make sure no stale source-tree libStorageDirect.so
    # shadows the one bundled inside the installed wheel.
    if [ -L "${PY_CLIENT_SRC_DIR}/libStorageDirect.so" ] || [ -f "${PY_CLIENT_SRC_DIR}/libStorageDirect.so" ]; then
        echo "Removing stale ${PY_CLIENT_SRC_DIR}/libStorageDirect.so so the installed wheel's copy is used"
        rm -f "${PY_CLIENT_SRC_DIR}/libStorageDirect.so"
    fi

    # Install into an ephemeral venv so we don't leak state into the runner's
    # system Python (the CI agent's workspace is persistent across jobs).
    DACS_WHEEL_VENV="$(mktemp -d -t dacs-wheel-venv-XXXXXX)"
    trap 'rm -rf "$DACS_WHEEL_VENV"' EXIT
    echo "Creating ephemeral venv: $DACS_WHEEL_VENV"
    python3 -m venv "$DACS_WHEEL_VENV"
    # shellcheck disable=SC1091
    source "$DACS_WHEEL_VENV/bin/activate"
    python -m pip install \
        --disable-pip-version-check \
        --require-hashes \
        -r "$TEST_REQUIREMENTS"

    echo "Installing dacs-client wheel: $WHEEL_PATH"
    python -m pip install --force-reinstall --no-deps "$WHEEL_PATH"

    echo "Installed package location:"
    python3 -c "import py_dacs_client, os; print('  py_dacs_client ->', os.path.dirname(py_dacs_client.__file__))"
else
    # ---- source mode (developer default) --------------------------------
    if [ "$DACS_TEST_MODE" != "source" ]; then
        echo "WARNING: unknown DACS_TEST_MODE=$DACS_TEST_MODE, falling back to 'source'"
    fi

    python -m pip install \
        --disable-pip-version-check \
        --require-hashes \
        -r "$TEST_REQUIREMENTS"

    # PYTHONPATH so Python finds py_dacs_client from the source tree.
    export PYTHONPATH="${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client:${PY_CLIENT_SRC_DIR}:${PYTHONPATH}"

    # Symlink libStorageDirect.so from bin/ into the source tree so the loader
    # finds it next to py_dacs_client/.
    NATIVE_LIB_PATH="${REPOSITORY_ROOT}/bin/${BUILD_TYPE}/${BUILD_ARCH}/native-storage/storage"
    echo "Native Lib Path: $NATIVE_LIB_PATH"
    if [ -d "$NATIVE_LIB_PATH" ]; then
        export LD_LIBRARY_PATH="${NATIVE_LIB_PATH}:${LD_LIBRARY_PATH}"
        LIB_DIRECT_SOURCE="${NATIVE_LIB_PATH}/libStorageDirect.so"
        LIB_DIRECT_TARGET="${PY_CLIENT_SRC_DIR}/libStorageDirect.so"
        if [ -f "$LIB_DIRECT_SOURCE" ] && [ ! -e "$LIB_DIRECT_TARGET" ]; then
            ln -sf "$LIB_DIRECT_SOURCE" "$LIB_DIRECT_TARGET"
            echo "Symlinked: $LIB_DIRECT_TARGET -> $LIB_DIRECT_SOURCE"
        elif [ -e "$LIB_DIRECT_TARGET" ]; then
            echo "libStorageDirect.so already present at $LIB_DIRECT_TARGET"
        else
            echo "WARNING: libStorageDirect.so not found at $LIB_DIRECT_SOURCE"
        fi
    else
        echo "WARNING: native lib path not found: $NATIVE_LIB_PATH (run a native build first)"
    fi
fi

# Test root directory
TEST_ROOT="${REPOSITORY_ROOT}/src/subsystems/storage/dacs_client/py_dacs_client/tests"

if [ ! -d "$TEST_ROOT" ]; then
    echo "Test directory not found: $TEST_ROOT"
    exit 1
fi
echo "Found test directory: $TEST_ROOT"

# Directories to exclude from the default test run
EXCLUDE_DIRS=(
    "blobfile_base_tests"
    "consistent_mode"
)

# Build ignore options for excluded directories
IGNORE_OPTS=""
for exclude_dir in "${EXCLUDE_DIRS[@]}"; do
    IGNORE_OPTS="$IGNORE_OPTS --ignore=${TEST_ROOT}/${exclude_dir}"
done

# -----------------------------------------------
# run_pytest: Run a single pytest invocation
# -----------------------------------------------
# Usage: run_pytest <label> <xml_suffix> <test_paths...>
#
# Common args (timeout, verbosity, traceback, faulthandler) are defined once here.
# The caller controls which directories/files to run and can set env vars before calling.
run_pytest() {
    local label="$1"
    local xml_suffix="$2"
    shift 2

    echo ""
    echo "========================================"
    echo "Running ${label} tests"
    echo "========================================"

    local xml_file="${REPOSITORY_ROOT}/pytest-results${xml_suffix:+-${xml_suffix}}.xml"

    python -X faulthandler -m pytest \
        --timeout=300 \
        -vv \
        -s \
        --tb=long \
        --junitxml="${xml_file}" \
        "$@"
}

echo ""
echo "Running pytest..."
echo "----------------------------------------"

# Debug: Print environment and library information
echo ""
echo "=== Debug Information ==="
echo "Python version: $(python3 --version 2>&1)"
echo "Pytest version: $(pytest --version 2>&1)"
echo "LD_LIBRARY_PATH: ${LD_LIBRARY_PATH:-not set}"
echo "PYTHONPATH: ${PYTHONPATH:-not set}"
echo ""

# Check if native libraries exist and are valid
echo "=== Native Library Check ==="
if [ "$DACS_TEST_MODE" = "wheel" ]; then
    INSTALLED_PKG_DIR=$(python3 -c "import py_dacs_client, os; print(os.path.dirname(py_dacs_client.__file__))" 2>/dev/null || echo "")
    if [ -n "$INSTALLED_PKG_DIR" ]; then
        echo "py_dacs_client installed at: $INSTALLED_PKG_DIR"
        LIB_PATH="${INSTALLED_PKG_DIR}/libStorageDirect.so"
    else
        echo "ERROR: py_dacs_client is not importable - wheel install likely failed"
        LIB_PATH=""
    fi
else
    LIB_PATH="${PY_CLIENT_SRC_DIR}/libStorageDirect.so"
fi
if [ -n "$LIB_PATH" ] && [ -f "$LIB_PATH" ]; then
    echo "libStorageDirect.so exists at $LIB_PATH"
    ls -la "$LIB_PATH"
    if command -v file >/dev/null 2>&1; then
        file "$LIB_PATH" 2>&1 || echo "file command failed"
    else
        echo "file command not installed; skipping"
    fi
    if command -v ldd >/dev/null 2>&1; then
        ldd "$LIB_PATH" 2>&1 || echo "ldd command failed"
    else
        echo "ldd command not installed; skipping"
    fi
elif [ -n "$LIB_PATH" ]; then
    echo "WARNING: libStorageDirect.so NOT FOUND at $LIB_PATH"
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

# -----------------------------------------------
# Run test groups
# -----------------------------------------------
# Each group runs in a separate Python process.
# To add a new isolated test group:
#   1. Create a subdirectory under tests/ (e.g., tests/my_mode/)
#   2. Add the directory name to EXCLUDE_DIRS above
#   3. Add a run_pytest call below with the required env vars
# -----------------------------------------------

PYTEST_EXIT_CODE=0

# Group 1: Default tests (everything except excluded directories)
run_pytest "default" "" $IGNORE_OPTS "${TEST_ROOT}" "$@"
PYTEST_EXIT_CODE=$?

# Group 2: Consistent-mode tests (run in a separate pytest invocation)
if [ $PYTEST_EXIT_CODE -eq 0 ] && [ -d "${TEST_ROOT}/consistent_mode" ]; then
    run_pytest "consistent-mode" "consistent-mode" "${TEST_ROOT}/consistent_mode" "$@"
    CM_EXIT_CODE=$?
    if [ $CM_EXIT_CODE -ne 0 ]; then
        PYTEST_EXIT_CODE=$CM_EXIT_CODE
    fi
fi

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
