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

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

readonly PIP_TOOLS_VERSION=7.6.1
readonly MINIMUM_PIP_MAJOR_VERSION=26
readonly PYTHON_VERSION=3.10
readonly PYPI_INDEX_URL=https://pypi.org/simple
readonly PYPI_UPLOAD_DELAY=P7D
readonly PYTORCH_CPU_INDEX_URL=https://download.pytorch.org/whl/cpu
readonly RUNAI_REQUIREMENTS="Docker/requirements/runai-model-streamer.in"
readonly REQUIREMENT_INPUTS=(
    ".github/workflows/linter-requirements.in"
    ".pipelines/requirements/compile.in"
    "Docker/requirements/cache-client.in"
    "Docker/requirements/dacs-client.in"
    "$RUNAI_REQUIREMENTS"
    "src/images/native-build/requirements.in"
    ".pipelines/requirements/python-build.in"
    "src/subsystems/storage/dacs_client/requirements-test.in"
    "src/subsystems/storage/storage_intercept/integration_tests/requirements.in"
    "src/subsystems/storage/stress_tests/container/requirements.in"
)

installed_python_version="$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
if [[ "$installed_python_version" != "$PYTHON_VERSION" ]]; then
    echo "Python $PYTHON_VERSION is required; found $installed_python_version." >&2
    exit 1
fi

installed_pip_version="$(python3 -m pip --version | awk '{print $2}')"
installed_pip_major_version="${installed_pip_version%%.*}"
if [[ ! "$installed_pip_major_version" =~ ^[0-9]+$ ]] ||
   (( installed_pip_major_version < MINIMUM_PIP_MAJOR_VERSION )); then
    echo "pip $MINIMUM_PIP_MAJOR_VERSION or newer is required; found $installed_pip_version." >&2
    exit 1
fi

if ! command -v pip-compile >/dev/null 2>&1; then
    echo "pip-compile is required. Install pip-tools==$PIP_TOOLS_VERSION." >&2
    exit 1
fi

installed_version="$(pip-compile --version | awk '{print $NF}')"
if [[ "$installed_version" != "$PIP_TOOLS_VERSION" ]]; then
    echo "pip-tools $PIP_TOOLS_VERSION is required; found $installed_version." >&2
    exit 1
fi

cd "$REPOSITORY_ROOT"
for input in "${REQUIREMENT_INPUTS[@]}"; do
    output="${input%.in}.txt"
    index_args=(--no-emit-index-url)
    upload_args=(--uploaded-prior-to "$PYPI_UPLOAD_DELAY")
    if [[ "$input" == "$RUNAI_REQUIREMENTS" ]]; then
        index_args=(
            --emit-index-url
            --extra-index-url "$PYTORCH_CPU_INDEX_URL"
        )
        # The PyTorch index does not publish the upload-time metadata required
        # by pip's uploaded-prior-to filter.
        upload_args=()
    fi

    echo "Compiling $input"
    env -u PIP_NO_INDEX \
      PIP_CONFIG_FILE=/dev/null \
      PIP_INDEX_URL="$PYPI_INDEX_URL" \
      pip-compile \
        --allow-unsafe \
        --generate-hashes \
        --no-header \
        --no-strip-extras \
        --quiet \
        --output-file "$output" \
        "${upload_args[@]}" \
        "${index_args[@]}" \
        "$input"
done
