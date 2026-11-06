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

git_repository() {
    git -c "safe.directory=${REPOSITORY_ROOT}" -C "${REPOSITORY_ROOT}" "$@"
}

valid_number() {
    [[ "$1" == "0" || "$1" != 0* ]]
}

valid_core() {
    local number
    local max_component=18446744073709551615
    local LC_ALL=C
    for number in "$@"; do
        # Compare equal-length decimal strings without signed integer overflow.
        # shellcheck disable=SC2071
        if ! valid_number "${number}" ||
            (( ${#number} > ${#max_component} )) ||
            { (( ${#number} == ${#max_component} )) && [[ "${number}" > "${max_component}" ]]; }; then
            return 1
        fi
    done
}

valid_release_tag() {
    [[ "$1" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)(-(alpha|beta|rc)\.([0-9]+))?$ ]] &&
        valid_core "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}" &&
        { [[ -z "${BASH_REMATCH[6]:-}" ]] || valid_number "${BASH_REMATCH[6]}"; }
}

derive_source_version() {
    local commit_count
    local description
    local reachable_tags
    local sha
    local source_ref="${BUILD_SOURCEBRANCH:-${GITHUB_REF:-}}"
    local source_tag
    local tag_commit

    if [[ "${source_ref}" == refs/tags/* ]]; then
        source_tag="${source_ref#refs/tags/}"
        if ! valid_release_tag "${source_tag}"; then
            echo "Triggering tag '${source_tag}' is not a supported DACS version. Expected vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-{alpha|beta|rc}.NUMBER, with core components no greater than 18446744073709551615." >&2
            return 1
        fi
        if ! tag_commit="$(git_repository rev-parse --verify "${source_ref}^{commit}")"; then
            echo "Triggering tag '${source_tag}' is missing or does not resolve to a commit. Fetch the triggering tag before building." >&2
            return 1
        fi
        sha="$(git_repository rev-parse --verify HEAD)" || return
        if [[ "${tag_commit}" != "${sha}" ]]; then
            echo "Triggering tag '${source_tag}' points to '${tag_commit}', not checked-out HEAD '${sha}'." >&2
            return 1
        fi
        echo "${source_tag}"
        return
    fi

    if ! description="$(git_repository describe \
        --tags --long --match='v*' --abbrev=14 HEAD 2>&1)"; then
        if ! git_repository rev-parse --verify HEAD >/dev/null 2>&1; then
            echo "Unable to derive a version: ${description}" >&2
            return 1
        fi

        reachable_tags="$(git_repository tag --merged HEAD --list 'v*')"
        if [[ -z "${reachable_tags}" ]]; then
            commit_count="$(git_repository rev-list --count HEAD)"
            sha="$(git_repository rev-parse --short=14 HEAD)"
            echo "v0.1.0-alpha.0.${commit_count}+${sha}"
            return
        fi

        echo "Unable to describe HEAD from its reachable v* tags: ${description}" >&2
        return 1
    fi

    if [[ "${description}" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)-(alpha|beta|rc)\.([0-9]+)-(0|[1-9][0-9]*)-g([0-9a-f]{14,40})$ ]] &&
        valid_core "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}" &&
        valid_number "${BASH_REMATCH[5]}"; then
        echo "v${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.${BASH_REMATCH[3]}-${BASH_REMATCH[4]}.${BASH_REMATCH[5]}.${BASH_REMATCH[6]}+${BASH_REMATCH[7]}"
        return
    fi

    if [[ "${description}" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)-(0|[1-9][0-9]*)-g([0-9a-f]{14,40})$ ]] &&
        valid_core "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}"; then
        echo "v${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.${BASH_REMATCH[3]}-${BASH_REMATCH[4]}+${BASH_REMATCH[5]}"
        return
    fi

    echo "Reachable tag description '${description}' is not a supported DACS version. Expected vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-{alpha|beta|rc}.NUMBER, with core components no greater than 18446744073709551615." >&2
    return 1
}

to_python_version() {
    local version="$1"
    local prerelease

    if [[ "${version}" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)-(alpha|beta|rc)\.([0-9]+)\.(0|[1-9][0-9]*)\+([0-9a-f]{14,40})$ ]]; then
        prerelease="${BASH_REMATCH[2]}"
        [[ "${prerelease}" == "alpha" ]] && prerelease="a"
        [[ "${prerelease}" == "beta" ]] && prerelease="b"
        echo "${BASH_REMATCH[1]}${prerelease}${BASH_REMATCH[3]}.dev${BASH_REMATCH[4]}"
        return
    fi

    if [[ "${version}" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)-(0|[1-9][0-9]*)\+[0-9a-f]{14,40}$ ]]; then
        echo "${BASH_REMATCH[1]}.dev${BASH_REMATCH[2]}"
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

to_universal_version() {
    local version="${1#v}"
    echo "${version/+/.g}"
}

main() {
    local source_version
    local architecture_tag
    source_version="$(derive_source_version)" || return

    case "${FORMAT}" in
        source)
            echo "${source_version}"
            ;;
        container)
            architecture_tag="${source_version//+/_}-linux-amd64"
            if (( ${#architecture_tag} > 128 )); then
                echo "Container version '${source_version//+/_}' exceeds the 128-character tag limit with a -linux-amd64 or -linux-arm64 suffix." >&2
                return 1
            fi
            echo "${source_version//+/_}"
            ;;
        helm)
            echo "${source_version#v}"
            ;;
        rust)
            to_universal_version "${source_version}"
            ;;
        python)
            to_python_version "${source_version}"
            ;;
        universal)
            to_universal_version "${source_version}"
            ;;
        *)
            echo "Usage: $0 {source|container|helm|rust|python|universal}" >&2
            return 2
            ;;
    esac
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main
fi
