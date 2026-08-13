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
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# Load configuration
if [[ -f "$SCRIPT_DIR/config/local.config" ]]; then
    source "$SCRIPT_DIR/config/local.config"
    echo "Loaded config from: $SCRIPT_DIR/config/local.config"
fi

NAMESPACE="${NAMESPACE:-cache-server}"
RELEASE_NAME="${RELEASE_NAME:-cache-server}"
CACHE_SERVER_IMAGE="${CACHE_SERVER_IMAGE:-cache-server:local}"
CACHE_CLIENT_IMAGE="${CACHE_CLIENT_IMAGE:-cache-test-client:local}"
CACHE_SERVER_SCHEDULER_IMAGE="${CACHE_SERVER_SCHEDULER_IMAGE:-cacheserver-scheduler:local}"

# Load images into Minikube
echo "Loading images into Minikube..."
minikube image load "$CACHE_SERVER_IMAGE"
minikube image load "$CACHE_CLIENT_IMAGE"
minikube image load "$CACHE_SERVER_SCHEDULER_IMAGE"

echo "Deploying cache-server to Minikube (Namespace: $NAMESPACE, Release: $RELEASE_NAME)..."

cd "$REPOSITORY_ROOT/helm/cache-server"

# Create namespace if it doesn't exist
kubectl create namespace "$NAMESPACE" --dry-run=client -o yaml | kubectl apply -f - >/dev/null

# Uninstall existing release and clean up resources
helm uninstall "$RELEASE_NAME" -n "$NAMESPACE" >/dev/null 2>&1 || true
kubectl delete pods,svc,deployments,statefulsets,job -l app=cacheserver -n "$NAMESPACE" --wait >/dev/null 2>&1 || true

# Deploy with Helm, using image names from config
helm install "$RELEASE_NAME" . --namespace "$NAMESPACE" --values values.yaml \
  --set cacheServer.image.repository="${CACHE_SERVER_IMAGE%:*}" \
  --set cacheServer.image.tag="${CACHE_SERVER_IMAGE#*:}" \
  --set cacheServer.scheduler.image.repository="${CACHE_SERVER_SCHEDULER_IMAGE%:*}" \
  --set cacheServer.scheduler.image.tag="${CACHE_SERVER_SCHEDULER_IMAGE#*:}" \
  --wait --timeout=15m >/dev/null

# Wait for cacheserver pods to be ready
echo "Waiting for cacheserver pods to be ready..."
while true; do
  ready_pods=$(kubectl get pods -n "$NAMESPACE" -l app=cacheserver --field-selector=status.phase=Running --no-headers | wc -l)
  total_pods=$(kubectl get pods -n "$NAMESPACE" -l app=cacheserver --no-headers | wc -l)
  if [[ "$ready_pods" -eq "$total_pods" && "$total_pods" -gt 0 ]]; then
    echo "All cacheserver pods are ready."
    break
  fi
  sleep 5
done

# Display CacheServer deployment status
echo ""
echo "=========================================="
echo "CacheServer Deployment Status"
echo "=========================================="
echo ""

echo "StatefulSet:"
kubectl get statefulset cacheserver -n "$NAMESPACE"
echo ""

echo "Service:"
kubectl get svc cacheserver -n "$NAMESPACE"
echo ""

echo "ConfigMap:"
kubectl get configmap cacheserver-discovery-config -n "$NAMESPACE" -o jsonpath='{.data}'
echo ""

echo "Pods:"
kubectl get pods -n "$NAMESPACE" -l app=cacheserver -o wide
echo ""

echo "Scheduler Pods:"
kubectl get pods -n "$NAMESPACE" -l app=cacheserver-scheduler -o wide
echo ""

echo "Cache server deployed successfully!"

echo ""
echo "=========================================="
echo "CacheServer Logs (last 200 lines)"
echo "=========================================="
kubectl logs -n "$NAMESPACE" -l app=cacheserver --all-containers --prefix --tail=200

echo ""
echo "=========================================="
echo "Scheduler Logs (last 200 lines)"
echo "=========================================="
kubectl logs -n "$NAMESPACE" -l app=cacheserver-scheduler --all-containers --prefix --tail=200

echo ""
echo "=========================================="
echo "Useful Commands"
echo "=========================================="
echo "View cacheserver logs:  kubectl logs -n $NAMESPACE -l app=cacheserver --all-containers --prefix"
echo "View scheduler logs:    kubectl logs -n $NAMESPACE -l app=cacheserver-scheduler --all-containers --prefix"