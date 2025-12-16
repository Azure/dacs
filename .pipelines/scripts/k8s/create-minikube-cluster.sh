#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Load configuration
if [[ -f "$SCRIPT_DIR/config/local.config" ]]; then
    source "$SCRIPT_DIR/config/local.config"
    echo "Loaded config from: $SCRIPT_DIR/config/local.config"
fi

# Ensure prerequisites are installed
"$SCRIPT_DIR/prerequisites.sh"

# Cluster configuration (with defaults from config or hardcoded)
CLUSTER_NAME="${CLUSTER_NAME:-minikube}"
NUM_NODES="${MINIKUBE_NODES:-4}"
MEMORY="${MINIKUBE_MEMORY:-2048}"
CPUS="${MINIKUBE_CPUS:-2}"

echo "Cluster config: $NUM_NODES nodes, ${MEMORY}MB RAM, $CPUS CPUs"

# Check if cluster already exists and delete it
if minikube profile list 2>/dev/null | grep -q "$CLUSTER_NAME"; then
  echo "Existing Minikube cluster found. Deleting it..."
  minikube delete
  echo "Cluster deleted successfully!"
fi

echo "Creating Minikube cluster with $NUM_NODES nodes..."
minikube start --nodes="$NUM_NODES" --memory="$MEMORY" --cpus="$CPUS" --driver=docker --force
echo "Cluster created successfully!"

# Label nodes for cache server pod scheduling
echo "Labeling nodes for cache server scheduling..."
for node in $(kubectl get nodes -o jsonpath='{.items[*].metadata.name}'); do
  kubectl label nodes "$node" dacs.azure.com/processing-unit=cpu --overwrite
done

# Create the required directory on all nodes
echo "Setting up /var/lib/ssd/cacheserver on all nodes..."
for node in $(kubectl get nodes -o jsonpath='{.items[*].metadata.name}'); do
  minikube ssh -n "$node" -- "sudo mkdir -p /var/lib/ssd/cacheserver && sudo chmod 777 /var/lib/ssd/cacheserver"
done

# Verify the directory exists on all nodes
echo "Verifying /var/lib/ssd/cacheserver on all nodes..."
for node in $(kubectl get nodes -o jsonpath='{.items[*].metadata.name}'); do
  minikube ssh -n "$node" -- "ls -ld /var/lib/ssd/cacheserver" | awk '{print $1, $3, $4, $9}'
done

# Show node labels
echo ""
echo "Node labels:"
kubectl get nodes --show-labels | grep dacs.azure.com/processing-unit

echo ""
echo "Cluster setup complete!"