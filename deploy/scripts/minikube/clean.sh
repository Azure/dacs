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

CLUSTER_NAME="${CLUSTER_NAME:-minikube}"

echo "Deleting Minikube cluster '$CLUSTER_NAME'..."
minikube delete --profile "$CLUSTER_NAME"
echo "Cluster deleted successfully!"
