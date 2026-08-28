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
readonly REPOSITORY_ROOT="${DACS_REPOSITORY_ROOT:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
readonly FORMAT="${1:-source}"

valid_number() {
    [[ "$1" == "0" || "$1" != 0* ]]
}

valid_core() {
    valid_number "$1" && valid_number "$2" && valid_number "$3"
}

derive_source_version() {
    local description
    local reachable_tags

    if ! description="$(git -C "${REPOSITORY_ROOT}" describe \
        --tags --match='v*' --abbrev=14 HEAD 2>&1)"; then
        if ! git -C "${REPOSITORY_ROOT}" rev-parse --verify HEAD >/dev/null 2>&1; then
            echo "Unable to derive a version: ${description}" >&2
            return 1
        fi

        reachable_tags="$(git -C "${REPOSITORY_ROOT}" tag --merged HEAD --list 'v*')"
        if [[ -z "${reachable_tags}" ]]; then
            echo "v0.1.0"
            return
        fi

        echo "Unable to describe HEAD from its reachable v* tags: ${description}" >&2
        return 1
    fi

    if [[ "${description}" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)(-(alpha|beta|rc)\.([0-9]+))?$ ]] &&
        valid_core "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}" &&
        { [[ -z "${BASH_REMATCH[6]:-}" ]] || valid_number "${BASH_REMATCH[6]}"; }; then
        echo "${description}"
        return
    fi

    if [[ "${description}" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)-(alpha|beta|rc)\.([0-9]+)-([1-9][0-9]*)-g([0-9a-f]{14,40})$ ]] &&
        valid_core "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}" &&
        valid_number "${BASH_REMATCH[5]}"; then
        echo "v${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.${BASH_REMATCH[3]}-${BASH_REMATCH[4]}.${BASH_REMATCH[5]}.${BASH_REMATCH[6]}+${BASH_REMATCH[7]}"
        return
    fi

    if [[ "${description}" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)-[1-9][0-9]*-g[0-9a-f]{14,40}$ ]] &&
        valid_core "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}"; then
        echo "Cannot derive a development version from stable tag 'v${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.${BASH_REMATCH[3]}': no next release line was selected. Create a prerelease v* tag for the intended release line." >&2
        return 1
    fi

    echo "Reachable tag description '${description}' is not a supported DACS version. Expected vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-{alpha|beta|rc}.NUMBER." >&2
    return 1
}

to_python_version() {
    local version="$1"
    local prerelease

    if [[ "${version}" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)-(alpha|beta|rc)\.([0-9]+)\.([1-9][0-9]*)\+([0-9a-f]{14,40})$ ]]; then
        prerelease="${BASH_REMATCH[2]}"
        [[ "${prerelease}" == "alpha" ]] && prerelease="a"
        [[ "${prerelease}" == "beta" ]] && prerelease="b"
        echo "${BASH_REMATCH[1]}${prerelease}${BASH_REMATCH[3]}.dev${BASH_REMATCH[4]}+${BASH_REMATCH[5]}"
        return
    fi

    if [[ "${version}" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)(-(alpha|beta|rc)\.([0-9]+))?$ ]]; then
        if [[ -z "${BASH_REMATCH[3]}" ]]; then
            echo "${BASH_REMATCH[1]}"
            return
        fi
        prerelease="${BASH_REMATCH[3]}"
        [[ "${prerelease}" == "alpha" ]] && prerelease="a"
        [[ "${prerelease}" == "beta" ]] && prerelease="b"
        echo "${BASH_REMATCH[1]}${prerelease}${BASH_REMATCH[4]}"
        return
    fi

    echo "Cannot convert source version '${version}' to PEP 440." >&2
    return 1
}

main() {
    local source_version
    source_version="$(derive_source_version)"

    case "${FORMAT}" in
        source)
            echo "${source_version}"
            ;;
        container)
            echo "${source_version//+/_}"
            ;;
        helm | rust)
            echo "${source_version#v}"
            ;;
        python)
            to_python_version "${source_version}"
            ;;
        *)
            echo "Usage: $0 {source|container|helm|rust|python}" >&2
            return 2
            ;;
    esac
}

main
