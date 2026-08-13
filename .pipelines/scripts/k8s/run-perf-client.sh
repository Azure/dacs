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

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Load configuration
if [[ -f "$SCRIPT_DIR/config/local.config" ]]; then
    source "$SCRIPT_DIR/config/local.config"
fi

MODE=${1:-upload}
NAMESPACE="${NAMESPACE:-default}"

# Use config values for perf client parameters
NUM_THREADS="${PERF_NUM_THREADS:-1}"
DATA_SIZE_GIB="${PERF_DATA_SIZE_GIB:-1}"
NUM_ITERATIONS="${PERF_NUM_ITERATIONS:-1}"

if [[ "$MODE" != "upload" && "$MODE" != "download" ]]; then
    echo "Usage: $0 <upload|download>"
    echo ""
    echo "Examples:"
    echo "  $0 upload"
    echo "  $0 download"
    exit 1
fi

echo "Running PerfClient in $MODE mode (threads=$NUM_THREADS, size=${DATA_SIZE_GIB}GiB, iterations=$NUM_ITERATIONS)..."

# Delete existing job
kubectl delete job ${MODE}-client -n "$NAMESPACE" --ignore-not-found=true >/dev/null

# Apply the template with mode substitution AND config parameters
sed -e "s/MODE_PLACEHOLDER/$MODE/g" \
    -e "s/NAMESPACE_PLACEHOLDER/$NAMESPACE/g" \
    -e "s/'--num-threads', '[0-9]*'/'--num-threads', '$NUM_THREADS'/g" \
    -e "s/'--data-size-GiB', '[0-9]*'/'--data-size-GiB', '$DATA_SIZE_GIB'/g" \
    -e "s/'--num-iterations', '[0-9]*'/'--num-iterations', '$NUM_ITERATIONS'/g" \
    "$SCRIPT_DIR/manifests/perf-client.yaml" | kubectl apply -f - >/dev/null

# Wait for job to complete
echo "Waiting for job to complete..."
kubectl wait --for=condition=complete job/${MODE}-client -n "$NAMESPACE" --timeout=300s >/dev/null || true

echo ""
echo "=========================================="
echo "${MODE^} Client Logs"
echo "=========================================="
kubectl logs job/${MODE}-client -n "$NAMESPACE" --tail=100

# Show job status
echo ""
kubectl get job ${MODE}-client -n "$NAMESPACE"