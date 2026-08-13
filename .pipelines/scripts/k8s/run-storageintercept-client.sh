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

NAMESPACE="${NAMESPACE:-default}"
STORAGE_CONFIG="${STORAGE_CONFIG_PATH:-config/storageIntercept.config}"

# Resolve relative path
if [[ ! "$STORAGE_CONFIG" = /* ]]; then
    STORAGE_CONFIG="$SCRIPT_DIR/$STORAGE_CONFIG"
fi

echo "Running Storage Intercept Client..."
echo "Using config: $STORAGE_CONFIG"

# Validate config exists
if [[ ! -f "$STORAGE_CONFIG" ]]; then
    echo "ERROR: Config file not found: $STORAGE_CONFIG"
    exit 1
fi

# Delete existing job and configmap
kubectl delete job storageintercept-client -n "$NAMESPACE" --ignore-not-found=true >/dev/null
kubectl delete configmap storageintercept-config -n "$NAMESPACE" --ignore-not-found=true >/dev/null

# Create ConfigMap from storageIntercept.config file
echo "Creating ConfigMap from storageIntercept.config..."
kubectl create configmap storageintercept-config \
  --from-file=storageIntercept.config="$STORAGE_CONFIG" \
  -n "$NAMESPACE" >/dev/null

# Apply the YAML
kubectl apply -f "$SCRIPT_DIR/manifests/storageintercept-client.yaml" >/dev/null

# Wait for job to complete
echo "Waiting for job to complete..."
kubectl wait --for=condition=complete job/storageintercept-client -n "$NAMESPACE" --timeout=300s >/dev/null || true

echo ""
echo "=========================================="
echo "Storage Intercept Client Logs"
echo "=========================================="
kubectl logs job/storageintercept-client -n "$NAMESPACE" --tail=100

# Show job status
echo ""
kubectl get job storageintercept-client -n "$NAMESPACE"