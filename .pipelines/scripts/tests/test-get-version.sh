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
readonly VERSION_HELPER="${SCRIPT_DIR}/../get-version.sh"
TEST_REPOSITORY=""

unset BUILD_SOURCEBRANCH GITHUB_REF

cleanup() {
    if [[ -n "${TEST_REPOSITORY}" ]]; then
        rm -rf -- "${TEST_REPOSITORY}"
    fi
}
trap cleanup EXIT

assert_version() {
    local expected="$1"
    local format="$2"
    local actual
    shift 2

    actual="$(env DACS_REPOSITORY_ROOT="${TEST_REPOSITORY}" "$@" "${VERSION_HELPER}" "${format}")"
    if [[ "${actual}" != "${expected}" ]]; then
        echo "Expected ${format} version '${expected}', got '${actual}'." >&2
        return 1
    fi
}

assert_version_failure() {
    local expected="$1"
    local format="$2"
    local actual
    shift 2

    if actual="$(env DACS_REPOSITORY_ROOT="${TEST_REPOSITORY}" "$@" "${VERSION_HELPER}" "${format}" 2>&1)"; then
        echo "Expected version derivation to fail for '$*', got '${actual}'." >&2
        return 1
    fi
    if [[ "${actual}" != *"${expected}"* ]]; then
        echo "Expected error containing '${expected}', got '${actual}'." >&2
        return 1
    fi
}

assert_universal_conversion() {
    local expected="$1"
    local source_version="$2"
    local actual

    actual="$(bash -c '
        helper="$1"
        source_version="$2"
        shift 2
        source "${helper}"
        to_universal_version "${source_version}"
    ' _ "${VERSION_HELPER}" "${source_version}")"
    if [[ "${actual}" != "${expected}" ]]; then
        echo "Expected universal version '${expected}', got '${actual}'." >&2
        return 1
    fi
}

TEST_REPOSITORY="$(mktemp -d)"
git -C "${TEST_REPOSITORY}" init --quiet
git -C "${TEST_REPOSITORY}" config user.email "test@example.com"
git -C "${TEST_REPOSITORY}" config user.name "DACS Version Test"
touch "${TEST_REPOSITORY}/first"
git -C "${TEST_REPOSITORY}" add first
git -C "${TEST_REPOSITORY}" commit --quiet -m "first"
touch "${TEST_REPOSITORY}/second"
git -C "${TEST_REPOSITORY}" add second
git -C "${TEST_REPOSITORY}" commit --quiet -m "second"

sha="$(git -C "${TEST_REPOSITORY}" rev-parse --short=14 HEAD)"
readonly sha
assert_version "v0.1.0-alpha.0.2+${sha}" source
assert_version "v0.1.0-alpha.0.2_${sha}" container
assert_version "0.1.0-alpha.0.2+${sha}" helm
assert_version "0.1.0-alpha.0.2.g${sha}" rust
assert_version "0.1.0a0.dev2" python
assert_version "0.1.0-alpha.0.2.g${sha}" universal
assert_universal_conversion \
    "0.1.0-alpha.0.2.g01234567890123" \
    "v0.1.0-alpha.0.2+01234567890123"

git -C "${TEST_REPOSITORY}" tag v0.1.0
for provider in BUILD_SOURCEBRANCH GITHUB_REF; do
    for source_ref in "" refs/heads/main refs/pull/123/merge; do
        assert_version "v0.1.0-0+${sha}" source "${provider}=${source_ref}"
        assert_version "v0.1.0-0_${sha}" container "${provider}=${source_ref}"
        assert_version "0.1.0-0+${sha}" helm "${provider}=${source_ref}"
        assert_version "0.1.0.dev0" python "${provider}=${source_ref}"
        assert_version "0.1.0-0.g${sha}" rust "${provider}=${source_ref}"
        assert_version "0.1.0-0.g${sha}" universal "${provider}=${source_ref}"
    done
    for format in source container; do
        assert_version "v0.1.0" "${format}" "${provider}=refs/tags/v0.1.0"
    done
    for format in helm python rust universal; do
        assert_version "0.1.0" "${format}" "${provider}=refs/tags/v0.1.0"
    done
done

git -C "${TEST_REPOSITORY}" tag --delete v0.1.0 >/dev/null
for prerelease in alpha beta rc; do
    git -C "${TEST_REPOSITORY}" tag "v0.2.0-${prerelease}.1"
    python_prerelease="${prerelease}"
    [[ "${prerelease}" == "alpha" ]] && python_prerelease="a"
    [[ "${prerelease}" == "beta" ]] && python_prerelease="b"
    for provider in BUILD_SOURCEBRANCH GITHUB_REF; do
        for source_ref in "" refs/heads/main refs/pull/123/merge; do
            assert_version "v0.2.0-${prerelease}.1.0+${sha}" source "${provider}=${source_ref}"
            assert_version "v0.2.0-${prerelease}.1.0_${sha}" container "${provider}=${source_ref}"
            assert_version "0.2.0-${prerelease}.1.0+${sha}" helm "${provider}=${source_ref}"
            assert_version "0.2.0${python_prerelease}1.dev0" python "${provider}=${source_ref}"
            assert_version "0.2.0-${prerelease}.1.0.g${sha}" rust "${provider}=${source_ref}"
            assert_version "0.2.0-${prerelease}.1.0.g${sha}" universal "${provider}=${source_ref}"
        done
        for format in source container; do
            assert_version "v0.2.0-${prerelease}.1" "${format}" "${provider}=refs/tags/v0.2.0-${prerelease}.1"
        done
        for format in helm rust universal; do
            assert_version "0.2.0-${prerelease}.1" "${format}" "${provider}=refs/tags/v0.2.0-${prerelease}.1"
        done
        assert_version "0.2.0${python_prerelease}1" python "${provider}=refs/tags/v0.2.0-${prerelease}.1"
    done
    git -C "${TEST_REPOSITORY}" tag --delete "v0.2.0-${prerelease}.1" >/dev/null
done
git -C "${TEST_REPOSITORY}" tag v0.2.0-rc.1

touch "${TEST_REPOSITORY}/third"
git -C "${TEST_REPOSITORY}" add third
git -C "${TEST_REPOSITORY}" commit --quiet -m "third"
dev_sha="$(git -C "${TEST_REPOSITORY}" rev-parse --short=14 HEAD)"
readonly dev_sha
assert_version "v0.2.0-rc.1.1+${dev_sha}" source
assert_version "0.2.0rc1.dev1" python
assert_version "0.2.0-rc.1.1.g${dev_sha}" rust
assert_version "0.2.0-rc.1.1.g${dev_sha}" universal

# git describe prefers the annotated RC over the lightweight stable tag.
git -C "${TEST_REPOSITORY}" tag -a v0.2.0-rc.2 -m "Release candidate"
git -C "${TEST_REPOSITORY}" tag v0.2.0
assert_version "v0.2.0-rc.2.0+${dev_sha}" source
assert_version "v0.2.0-rc.2.0+${dev_sha}" source \
    BUILD_SOURCEBRANCH=refs/heads/main GITHUB_REF=refs/tags/v0.2.0
assert_version "v0.2.0" source \
    BUILD_SOURCEBRANCH=refs/tags/v0.2.0 GITHUB_REF=refs/tags/v0.2.0-rc.2
assert_version "v0.2.0" source \
    BUILD_SOURCEBRANCH= GITHUB_REF=refs/tags/v0.2.0

for provider in BUILD_SOURCEBRANCH GITHUB_REF; do
    for format in source container; do
        assert_version "v0.2.0" "${format}" "${provider}=refs/tags/v0.2.0"
        assert_version "v0.2.0-rc.2" "${format}" "${provider}=refs/tags/v0.2.0-rc.2"
    done
    for format in helm rust universal; do
        assert_version "0.2.0" "${format}" "${provider}=refs/tags/v0.2.0"
        assert_version "0.2.0-rc.2" "${format}" "${provider}=refs/tags/v0.2.0-rc.2"
    done
    assert_version "0.2.0" python "${provider}=refs/tags/v0.2.0"
    assert_version "0.2.0rc2" python "${provider}=refs/tags/v0.2.0-rc.2"

    assert_version "v0.2.0-rc.2.0+${dev_sha}" source "${provider}=refs/heads/main"
    assert_version "v0.2.0-rc.2.0+${dev_sha}" source "${provider}=refs/pull/123/merge"
    assert_version_failure "not checked-out HEAD" source "${provider}=refs/tags/v0.2.0-rc.1"
    assert_version_failure "missing or does not resolve to a commit" source "${provider}=refs/tags/v9.9.9"

    for tag in v01.2.3 v1.02.3 v1.2.03 v1.2.3-rc.01 v1.2.3-preview.1 v1.2.3+metadata; do
        assert_version_failure "not a supported DACS version" source "${provider}=refs/tags/${tag}"
    done
done

git -C "${TEST_REPOSITORY}" tag --delete v0.2.0-rc.2 v0.2.0 >/dev/null
git -C "${TEST_REPOSITORY}" tag v0.3.0
touch "${TEST_REPOSITORY}/fourth"
git -C "${TEST_REPOSITORY}" add fourth
git -C "${TEST_REPOSITORY}" commit --quiet -m "fourth"
for distance in 1 2 3; do
    stable_dev_sha="$(git -C "${TEST_REPOSITORY}" rev-parse --short=14 HEAD)"
    for provider in BUILD_SOURCEBRANCH GITHUB_REF; do
        for source_ref in "" refs/heads/main refs/pull/123/merge; do
            assert_version "v0.3.0-${distance}+${stable_dev_sha}" source "${provider}=${source_ref}"
            assert_version "v0.3.0-${distance}_${stable_dev_sha}" container "${provider}=${source_ref}"
            assert_version "0.3.0-${distance}+${stable_dev_sha}" helm "${provider}=${source_ref}"
            assert_version "0.3.0.dev${distance}" python "${provider}=${source_ref}"
            assert_version "0.3.0-${distance}.g${stable_dev_sha}" rust "${provider}=${source_ref}"
            assert_version "0.3.0-${distance}.g${stable_dev_sha}" universal "${provider}=${source_ref}"
        done
        assert_version_failure "not checked-out HEAD" source "${provider}=refs/tags/v0.3.0"
    done
    if [[ "${distance}" != 3 ]]; then
        git -C "${TEST_REPOSITORY}" commit --allow-empty --quiet -m "after stable ${distance}"
    fi
done

boundary_sha="$(git -C "${TEST_REPOSITORY}" rev-parse --short=14 HEAD)"
for core in \
    18446744073709551615.2.3 \
    1.18446744073709551615.3 \
    1.2.18446744073709551615; do
    git -C "${TEST_REPOSITORY}" tag "v${core}"
    assert_version "v${core}-0+${boundary_sha}" source
    assert_version "${core}-0+${boundary_sha}" helm
    assert_version "${core}-0.g${boundary_sha}" rust
    assert_version "v${core}" source "GITHUB_REF=refs/tags/v${core}"
    git -C "${TEST_REPOSITORY}" tag --delete "v${core}" >/dev/null
done

for core in \
    18446744073709551616.2.3 \
    1.18446744073709551616.3 \
    1.2.18446744073709551616 \
    9999999999999999999999999999999999999999999999999999999999999999.2.3; do
    git -C "${TEST_REPOSITORY}" tag "v${core}"
    for format in source container helm python rust universal; do
        for source_ref in "" refs/heads/main refs/pull/123/merge "refs/tags/v${core}"; do
            assert_version_failure "core components no greater than 18446744073709551615" \
                "${format}" "GITHUB_REF=${source_ref}"
        done
    done
    git -C "${TEST_REPOSITORY}" tag --delete "v${core}" >/dev/null
done

for length in 89 90 102 106 107 120; do
    printf -v prerelease_number '%*s' "${length}" ''
    prerelease_number="${prerelease_number// /9}"
    tag="v1.2.3-rc.${prerelease_number}"
    git -C "${TEST_REPOSITORY}" tag "${tag}"
    for source_ref in "" refs/heads/main refs/pull/123/merge "refs/tags/${tag}"; do
        container_version="${tag}"
        if [[ "${source_ref}" != refs/tags/* ]]; then
            container_version+=".0_${boundary_sha}"
        fi
        if (( ${#container_version} <= 116 )); then
            assert_version "${container_version}" container "GITHUB_REF=${source_ref}"
        else
            assert_version_failure "exceeds the 128-character tag limit" \
                container "GITHUB_REF=${source_ref}"
        fi
    done
    git -C "${TEST_REPOSITORY}" tag --delete "${tag}" >/dev/null
done
