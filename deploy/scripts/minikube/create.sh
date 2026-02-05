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

# Configure minikube to use /mnt if available (CI environments have limited root disk)
if [ -d "/mnt" ] && [ "$(df /mnt 2>/dev/null | tail -1 | awk '{print $4}')" -gt 104857600 ]; then
    export MINIKUBE_HOME="/mnt/minikube"
    sudo mkdir -p "$MINIKUBE_HOME"
    sudo chown -R $(whoami):$(whoami) "$MINIKUBE_HOME"
    echo "Using MINIKUBE_HOME=$MINIKUBE_HOME (to avoid filling root disk)"
fi

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
echo -e "\nStep 1: Creating control-plane node..."
minikube start \
  --nodes=1 \
  --memory="$MEMORY" \
  --cpus="$CPUS" \
  --driver=docker \
  --force \
  --cni=auto \
  --container-runtime=containerd \
  --extra-config=kubelet.cgroup-driver=systemd \
  --extra-config=kubelet.housekeeping-interval=10s \
  --extra-config=kubelet.node-status-update-frequency=10s \
  --extra-config=kubelet.max-pods=110 \
  --extra-config=kubelet.fail-swap-on=false \
  --extra-config=kubelet.root-dir=/mnt/kubelet \
  --wait-timeout=10m \
  --wait=all \
  -v=1

# Add worker nodes one at a time with delays to reduce resource contention
WORKERS=$((NUM_NODES - 1))
echo -e "\nStep 2: Adding $WORKERS worker nodes sequentially..."
for i in $(seq 2 $NUM_NODES); do
  NODE_NUM=$(printf "%02d" $i)
  NODE_NAME="minikube-m${NODE_NUM}"
  echo -e "\n--- Adding worker node $i of $NUM_NODES (${NODE_NAME}) ---"
  
  minikube node add --worker=true -v=1
  
  echo "Node $i added successfully, waiting for stabilization..."
  sleep 15
done

echo "All nodes added, waiting for final stabilization..."
sleep 15

echo "Cluster created successfully!"

# Verify CNI is working on all nodes (especially important for node 5)
echo -e "\nVerifying CNI pods are running..."
kubectl get pods -n kube-system -l k8s-app=kindnet -o wide || echo "Warning: Could not get CNI pod status"

# Restart network components to ensure all nodes are fully ready
echo "Checking kube-proxy health on all nodes..."
kubectl get pods -n kube-system -l k8s-app=kube-proxy -o wide

# Check if any kube-proxy pods are not running
NOT_RUNNING=$(kubectl get pods -n kube-system -l k8s-app=kube-proxy --no-headers | grep -v "Running" | wc -l || true)
if [ "$NOT_RUNNING" -gt 0 ]; then
  echo "WARNING: $NOT_RUNNING kube-proxy pod(s) not running - restarting all kube-proxy pods..."
  kubectl delete pods -n kube-system -l k8s-app=kube-proxy || echo "Warning: Could not restart kube-proxy"
  echo "Waiting for kube-proxy to be ready..."
  sleep 20
  kubectl wait --for=condition=ready pod -l k8s-app=kube-proxy -n kube-system --timeout=120s || echo "Warning: kube-proxy pods may not be fully ready"
fi

echo "Restarting CNI pods to ensure all nodes networking..."
kubectl delete pods -n kube-system -l k8s-app=kindnet || echo "Warning: Could not restart CNI"
sleep 15
echo "Waiting for CNI to be ready..."
kubectl wait --for=condition=ready pod -l k8s-app=kindnet -n kube-system --timeout=120s || echo "Warning: CNI pods may not be fully ready"

# Label nodes for cache server pod scheduling
echo "Labeling nodes for cache server scheduling..."
for node in $(kubectl get nodes -o jsonpath='{.items[*].metadata.name}'); do
  kubectl label nodes "$node" dacs.azure.com/processing-unit=system --overwrite
  kubectl label nodes "$node" dacs.azure.com/processing-unit=cpu --overwrite
  echo "Labeled $node with system and cpu labels"
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
