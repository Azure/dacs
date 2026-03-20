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
CACHE_CONTROLLER_IMAGE="${CACHE_CONTROLLER_IMAGE:-cache-controller:local}"
AZURE_LINUX_IMAGE="mcr.microsoft.com/azurelinux/base/core:3.0"

# Load images into Minikube in parallel
echo "Loading images into Minikube..."
minikube image load "$CACHE_SERVER_IMAGE" &
minikube image load "$CACHE_CLIENT_IMAGE" &
minikube image load "$CACHE_SERVER_SCHEDULER_IMAGE" &
minikube image load "$CACHE_CONTROLLER_IMAGE" &
wait

# Pull and load Azure Linux image (used by iperf tests)
echo "Loading Azure Linux image for iperf tests..."
docker pull "$AZURE_LINUX_IMAGE"
minikube image load "$AZURE_LINUX_IMAGE"

echo "Images loaded successfully!"
