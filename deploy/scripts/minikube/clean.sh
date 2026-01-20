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

# Use /mnt for minikube if available
if [ -d "/mnt/minikube" ]; then
    export MINIKUBE_HOME="/mnt/minikube"
fi

CLUSTER_NAME="${CLUSTER_NAME:-minikube}"

echo "Deleting Minikube cluster '$CLUSTER_NAME'..."
minikube delete --profile "$CLUSTER_NAME"

# Clean up minikube data on /mnt if it exists
if [ -d "/mnt/minikube" ]; then
    echo "Cleaning up /mnt/minikube..."
    sudo rm -rf /mnt/minikube
fi

echo "Cluster deleted successfully!"
