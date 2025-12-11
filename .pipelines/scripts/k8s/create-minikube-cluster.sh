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
NUM_NODES="${MINIKUBE_NODES:-5}"
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
NODES=($(kubectl get nodes -o jsonpath='{.items[*].metadata.name}'))
FIRST_NODE=true
for node in "${NODES[@]}"; do
  if $FIRST_NODE; then
    # First node is system node for scheduler
    kubectl label nodes "$node" dacs.azure.com/processing-unit=system --overwrite
    echo "Labeled $node as system node"
    FIRST_NODE=false
  else
    # Other nodes are cpu nodes for cache servers
    kubectl label nodes "$node" dacs.azure.com/processing-unit=cpu --overwrite
    echo "Labeled $node as cpu node"
  fi
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
kubectl get nodes --show-labels | grep -E "(dacs\.azure\.com/processing-unit|dacs\.azure\.com/processing-unit)"

echo ""
echo "Cluster setup complete!"