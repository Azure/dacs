#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# Load configuration from .pipelines location (shared config)
CONFIG_FILE="$REPOSITORY_ROOT/.pipelines/scripts/k8s/config/local.config"
if [[ -f "$CONFIG_FILE" ]]; then
    source "$CONFIG_FILE"
    echo "Loaded config from: $CONFIG_FILE"
fi

CACHE_SERVER_IMAGE="${CACHE_SERVER_IMAGE:-cache-server:local}"
CACHE_CLIENT_IMAGE="${CACHE_CLIENT_IMAGE:-cache-test-client:local}"
CACHE_SERVER_SCHEDULER_IMAGE="${CACHE_SERVER_SCHEDULER_IMAGE:-cacheserver-scheduler:local}"

# Load images into Minikube
echo "Loading images into Minikube..."
minikube image load "$CACHE_SERVER_IMAGE"
minikube image load "$CACHE_CLIENT_IMAGE"
minikube image load "$CACHE_SERVER_SCHEDULER_IMAGE"

echo "Images loaded successfully!"
