#!/usr/bin/env bash

set -euo pipefail

# Function to display usage information
usage() {
    echo "Usage: $0 --resource-group <AKS_RESOURCE_GROUP> --location <AKS_LOCATION> --template <AKS_TEMPLATE> [--is-test <IS_TEST>] [--what-if]"
    echo "  --resource-group  AKS resource group name"
    echo "  --location        AKS location"
    echo "  --template        AKS template name"
    echo "  --is-test         (Optional) Flag to indicate if the deployment is a test (default: false)"
    echo "  --what-if         (Optional) Flag to indicate if the deployment should run in 'what-if' mode (default: false)"
    echo ""
    echo "Environment variables:"
    echo "  DOCKER_REGISTRY   (Optional) Docker registry to attach to AKS (default: dacs.azurecr.io)"
    exit 1
}

# Parse command-line arguments
IS_TEST=false
WHAT_IF=false
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --resource-group) AKS_RESOURCE_GROUP="$2"; shift ;;
        --location) AKS_LOCATION="$2"; shift ;;
        --template) AKS_TEMPLATE="$2"; shift ;;
        --is-test) IS_TEST="$2"; shift ;;
        --what-if) WHAT_IF=true ;;
        *) usage ;;
    esac
    shift
done

echo "AKS_RESOURCE_GROUP: $AKS_RESOURCE_GROUP"
echo "AKS_LOCATION: $AKS_LOCATION"
echo "AKS_TEMPLATE: $AKS_TEMPLATE"
echo "IS_TEST: $IS_TEST"
echo "WHAT_IF: $WHAT_IF"

# Ensure required arguments are provided and not just whitespace
if [[ -z "${AKS_RESOURCE_GROUP:-}" || "$AKS_RESOURCE_GROUP" =~ ^[[:space:]]*$ ]] || \
   [[ -z "${AKS_LOCATION:-}" || "$AKS_LOCATION" =~ ^[[:space:]]*$ ]] || \
   [[ -z "${AKS_TEMPLATE:-}" || "$AKS_TEMPLATE" =~ ^[[:space:]]*$ ]]; then
    echo "Error: Arguments cannot be empty or just whitespace."
    usage
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPLOY_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

if [[ "$WHAT_IF" == "true" ]]; then
    # Run a what-if deployment and extract node configuration
    WHAT_IF_OUTPUT=$(az deployment sub what-if \
        --name "$AKS_RESOURCE_GROUP-deployment" \
        --location "$AKS_LOCATION" \
        --template-file "$DEPLOY_DIR/template/main.bicep" \
        --parameters "$DEPLOY_DIR/parameters/$AKS_TEMPLATE.json" \
        --parameters resourceGroupName="$AKS_RESOURCE_GROUP" \
        --parameters isTest="$IS_TEST" \
        --no-pretty-print | \
    jq '
      # Find the AKS cluster resource
      .changes[] |
      select(.after.type == "Microsoft.ContainerService/managedClusters") |

      # Extract configuration from the agent pools
      .after.properties.agentPoolProfiles |

      # Return a clean object with the values we need
      map({
        name: .name,
        nodeCount: .count,
        vmSku: .vmSize,
        availabilityZones: (.availabilityZones // [])
      })
    ')
    echo "Requirements: $WHAT_IF_OUTPUT"
    exit 0
fi

# Check if the AKS cluster already exists
if az aks show --resource-group "$AKS_RESOURCE_GROUP" --name "$AKS_RESOURCE_GROUP-cluster" > /dev/null 2>&1; then
    echo "AKS cluster already exists in resource group $AKS_RESOURCE_GROUP"
else
    # Create the AKS cluster using Bicep template
    az deployment sub create \
        --name "$AKS_RESOURCE_GROUP-deployment" \
        --location "$AKS_LOCATION" \
        --template-file "$DEPLOY_DIR/template/main.bicep" \
        --parameters "$DEPLOY_DIR/parameters/$AKS_TEMPLATE.json" \
        --parameters resourceGroupName="$AKS_RESOURCE_GROUP" \
        --parameters isTest="$IS_TEST" \
        --what-if \
        --verbose

    az deployment sub create \
        --name "$AKS_RESOURCE_GROUP-deployment" \
        --location "$AKS_LOCATION" \
        --template-file "$DEPLOY_DIR/template/main.bicep" \
        --parameters "$DEPLOY_DIR/parameters/$AKS_TEMPLATE.json" \
        --parameters resourceGroupName="$AKS_RESOURCE_GROUP" \
        --parameters isTest="$IS_TEST" \
        --verbose
fi

# Attach ACR to the AKS cluster for pulling images (only if using Azure Container Registry)
# Use DOCKER_REGISTRY env var if set and ends with azurecr.io, otherwise use default
DEFAULT_ACR="dacs"
if [[ -z "${DOCKER_REGISTRY:-}" || "$DOCKER_REGISTRY" == *".azurecr.io" ]]; then
    if [[ -n "${DOCKER_REGISTRY:-}" ]]; then
        ACR_NAME="${DOCKER_REGISTRY%.azurecr.io}"
    else
        ACR_NAME="$DEFAULT_ACR"
    fi

    echo "Attaching ACR $ACR_NAME to AKS cluster..."
    if ! az aks update \
        --resource-group "$AKS_RESOURCE_GROUP" \
        --name "$AKS_RESOURCE_GROUP-cluster" \
        --attach-acr "$ACR_NAME" \
        --verbose 2>&1; then
        echo "Warning: Failed to attach ACR $ACR_NAME. You may need to manually configure image pull access."
        echo "  Option 1: az aks update -g $AKS_RESOURCE_GROUP -n $AKS_RESOURCE_GROUP-cluster --attach-acr <your-acr>"
        echo "  Option 2: Create an image pull secret for the cache-server namespace"
    fi
else
    echo "Skipping ACR attach (DOCKER_REGISTRY=$DOCKER_REGISTRY is not an Azure Container Registry)"
fi

# Get AKS cluster credentials
az aks get-credentials \
    --resource-group "$AKS_RESOURCE_GROUP" \
    --name "$AKS_RESOURCE_GROUP-cluster" \
    --overwrite-existing

# Apply custom Prometheus scrape settings if monitoring is enabled
PARAM_FILE="$DEPLOY_DIR/parameters/$AKS_TEMPLATE.json"
if jq -e '.parameters.azureMonitorWorkspaceId.value // empty' "$PARAM_FILE" > /dev/null 2>&1; then
    echo "Applying ama-metrics settings ConfigMap..."
    kubectl apply -f "$DEPLOY_DIR/ama-metrics-settings-configmap.yaml"
fi

echo ""
echo "AKS cluster created successfully!"
echo ""
echo "Next steps:"
echo "  1. Install dacs-prereq chart to set up NVMe RAID:"
echo "     helm install dacs-prereq ./helm/dacs-prereq -n cache-server --create-namespace"
echo ""
echo "  2. Install cache-server chart:"
echo "     helm install cache-server ./helm/cache-server -n cache-server"
