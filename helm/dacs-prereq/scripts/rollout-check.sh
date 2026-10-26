#!/bin/sh
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

set -eu

diagnostics() {
    status=$?
    if [ "$status" -ne 0 ]; then
        timeout 10s kubectl -n "$NAMESPACE" get daemonset "$DAEMONSET" -o yaml || echo "DaemonSet diagnostics unavailable" >&2
        timeout 10s kubectl -n "$NAMESPACE" get pods -l "$POD_SELECTOR" -o wide || echo "Pod diagnostics unavailable" >&2
        timeout 10s kubectl -n "$NAMESPACE" get events --sort-by=.metadata.creationTimestamp || echo "Event diagnostics unavailable" >&2
    fi
    exit "$status"
}

# Each operation receives its remaining budget; retries never reset the deadline.
retry() (
    deadline=$(($(date +%s) + $1))
    shift
    attempt=1
    while :; do
        remaining=$((deadline - $(date +%s)))
        if [ "$remaining" -le 0 ]; then
            echo "$* retry deadline exceeded" >&2
            exit 1
        fi
        if output=$("$@" "$remaining"); then
            printf '%s\n' "$output"
            exit 0
        else
            status=$?
        fi
        # Discard failed stdout from the result, but retain it for diagnostics.
        if [ -n "$output" ]; then
            printf '%s\n' "$output" >&2
        fi
        echo "$* attempt $attempt/3 failed (exit $status)" >&2
        remaining=$((deadline - $(date +%s)))
        if [ "$attempt" -ge 3 ] || [ "$remaining" -le 5 ]; then
            echo "$* exhausted its retry attempts or time budget" >&2
            exit "$status"
        fi
        sleep 5
        attempt=$((attempt + 1))
    done
)

rollout_status() {
    # Process timeouts preserve kubectl's automatic in-cluster authentication.
    timeout "${1}s" kubectl -n "$NAMESPACE" \
        rollout status "daemonset/$DAEMONSET" --timeout="${1}s"
}

desired_count() {
    timeout "${1}s" kubectl -n "$NAMESPACE" \
        get daemonset "$DAEMONSET" -o jsonpath='{.status.desiredNumberScheduled}'
}

trap diagnostics EXIT
retry "$ROLLOUT_TIMEOUT_SECONDS" rollout_status
# Check only after rollout status observes the current generation.
desired=$(retry 40 desired_count)
case "$desired" in
    ''|*[!0-9]*) echo "Invalid desired node count: $desired" >&2; exit 1 ;;
esac
if [ "$desired" -eq 0 ]; then
    echo "No nodes match the prerequisite DaemonSet" >&2
    exit 1
fi
echo "Prerequisite rollout complete on $desired nodes"
