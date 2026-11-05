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

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR/.."
BASE_DIR="$(pwd)"

LOCALBIN="${LOCALBIN:-$BASE_DIR/.local/bin}"
make helm LOCALBIN="$LOCALBIN"
export PATH="$LOCALBIN:$PATH"
export GOWORK=off

HELM_UNITTEST_VERSION="1.2.1"
export HELM_CONFIG_HOME="$BASE_DIR/.local/helm-tests/config"
export HELM_CACHE_HOME="$BASE_DIR/.local/helm-tests/cache"
export HELM_DATA_HOME="$BASE_DIR/.local/helm-tests/data"
export HELM_PLUGINS="$BASE_DIR/.local/helm-tests/plugins-$HELM_UNITTEST_VERSION"

installed_version="$(helm plugin list | awk '$1 == "unittest" {print $2}')"
if [ -z "$installed_version" ]; then
    helm plugin install https://github.com/helm-unittest/helm-unittest.git --version "v$HELM_UNITTEST_VERSION"
elif [ "$installed_version" != "$HELM_UNITTEST_VERSION" ]; then
    echo "Expected helm-unittest $HELM_UNITTEST_VERSION, found $installed_version in $HELM_PLUGINS" >&2
    exit 1
fi

helm dependency build "$BASE_DIR/src/subsystems/cache-controller/charts/dacs" --skip-refresh
helm dependency build "$BASE_DIR/helm/cache-server" --skip-refresh

helm unittest --strict --with-subchart=false \
    -f 'tests/*_test.yaml' \
    -f "$BASE_DIR/src/subsystems/cache-controller/charts/tests/registry_schema_test.yaml" \
    "$BASE_DIR/src/subsystems/cache-controller/charts/dacs" \
    "$BASE_DIR/helm/dacs-prereq" \
    "$BASE_DIR/helm/cache-server"

cd "$BASE_DIR/src/subsystems/cache-controller"
go test -race -tags=charttests ./charts -run "${1:-.}" -count=1
