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

resolve_build_cache_scope() {
    local scope="${1:-main}"
    local pull_request_id

    if [[ "$scope" == "pr" ]]; then
        pull_request_id="${SYSTEM_PULLREQUEST_PULLREQUESTID:-}"
        if [[ -z "$pull_request_id" &&
            "${BUILD_SOURCEBRANCH:-}" =~ ^refs/pull/([0-9]+)/ ]]; then
            pull_request_id="${BASH_REMATCH[1]}"
        fi
        scope="pr-${pull_request_id:-${BUILD_BUILDID:-local}}"
    fi

    if [[ ! "$scope" =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]*$ ]]; then
        echo "Invalid BUILD_CACHE_SCOPE: $scope" >&2
        return 1
    fi

    printf '%s\n' "$scope"
}
