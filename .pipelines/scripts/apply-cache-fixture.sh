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

# Apply a Cache fixture after its admission webhook becomes ready.
set -euo pipefail

if (( $# < 1 || $# > 2 )); then
  echo "Usage: $0 FIXTURE [STATIC_SERVER_COUNT]" >&2
  exit 1
fi

fixture=$1
readonly static_servers=${2:-0}
readonly max_webhook_attempts=24

if [[ ! -f "$fixture" ]]; then
  echo "Cache fixture does not exist: $fixture" >&2
  exit 1
fi
if [[ ! "$static_servers" =~ ^(0|[1-9][0-9]*)$ ]]; then
  echo "Cache static server count must be a positive integer or zero to use the fixture default" >&2
  exit 1
fi

echo "Applying Cache fixture: $fixture"
if [[ "$static_servers" != 0 ]]; then
  current_static_servers=$(kubectl patch --local --filename "$fixture" \
    --type=merge --patch '{}' --output='jsonpath={.spec.scaling.static.numServers}')
  if [[ -z "$current_static_servers" ]]; then
    echo "Cannot set cacheStaticServers on a dynamic Cache fixture; use zero to retain dynamic scaling" >&2
    exit 1
  fi
  rendered_fixture=$(mktemp)
  trap 'rm -f -- "$rendered_fixture"' EXIT
  kubectl patch --local --filename "$fixture" --type=json \
    --patch "[{\"op\":\"replace\",\"path\":\"/spec/scaling/static/numServers\",\"value\":$static_servers}]" \
    --output=yaml > "$rendered_fixture"
  fixture=$rendered_fixture
fi

for attempt in $(seq 1 "$max_webhook_attempts"); do
  if kubectl apply --dry-run=server --namespace cache-server --filename "$fixture" >/dev/null; then
    break
  fi
  if [[ "$attempt" -eq "$max_webhook_attempts" ]]; then
    echo "Cache webhook did not become ready after $max_webhook_attempts attempts" >&2
    exit 1
  fi
  echo "Cache webhook is not ready; retrying admission dry-run in 5 seconds"
  sleep 5
done
kubectl apply --namespace cache-server --filename "$fixture"
kubectl wait --namespace cache-server --filename "$fixture" --for=condition=Ready --timeout=10m
