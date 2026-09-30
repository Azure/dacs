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
readonly SCRIPT_DIR
REPOSITORY_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
readonly REPOSITORY_ROOT
TEST_DIR=""

cleanup() {
    if [[ -n "${TEST_DIR}" ]]; then
        rm -rf -- "${TEST_DIR}"
    fi
}
trap cleanup EXIT

TEST_DIR="$(mktemp -d)"
mkdir -p "${TEST_DIR}/bin"
cat >"${TEST_DIR}/bin/docker" <<'EOF'
#!/bin/bash
set -euo pipefail

case "${1:-} ${2:-}" in
    "image inspect")
        exit 1
        ;;
    "pull "*)
        printf 'pull-stdin=%s\n' "$(readlink "/proc/$$/fd/0")" \
            >>"${TEST_DOCKER_LOG:?}"
        exit 1
        ;;
    "build "*|"run "*)
        exit 0
        ;;
    *)
        echo "Unexpected docker command: $*" >&2
        exit 1
        ;;
esac
EOF
chmod +x "${TEST_DIR}/bin/docker"

printf 'unexpected credential input\n' |
    PATH="${TEST_DIR}/bin:${PATH}" \
    TEST_DOCKER_LOG="${TEST_DIR}/docker.log" \
    DOCKER_REGISTRY="example.azurecr.io" \
    "${REPOSITORY_ROOT}/bldenv.sh" --no-tty true >/dev/null

grep -Fx 'pull-stdin=/dev/null' "${TEST_DIR}/docker.log" >/dev/null
