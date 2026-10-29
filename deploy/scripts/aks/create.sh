#!/usr/bin/env bash

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

set -euo pipefail

# Function to display usage information
usage() {
    echo "Usage: $0 --resource-group <AKS_RESOURCE_GROUP> --location <AKS_LOCATION> --template <AKS_TEMPLATE> [--is-test <IS_TEST>] [--what-if]"
    echo "  --resource-group  AKS resource group name"
    echo "  --location        AKS location"
    echo "  --template        AKS template name"
    echo "  --is-test         (Optional) Flag to indicate if the deployment is a test (default: false)"
    echo "  --resource-prefix (Optional) Unique integration suite/run prefix in the existing dacs-integration resource group"
    echo "  --what-if         (Optional) Flag to indicate if the deployment should run in 'what-if' mode (default: false)"
    echo ""
    echo "Environment variables:"
    echo "  DOCKER_REGISTRY   (Optional) Docker registry to attach to AKS (default: dacstest.azurecr.io)"
    echo "  AKS_PARAMETER_FILE (Optional) Deployment parameter file instead of the named template"
    echo "  AKS_MIXED_NODE_OS (Optional) Mixed Linux integration profile (default: false)"
    echo "  AKS_NODE_ARCHITECTURE (Optional) Integration node architecture: amd64 or arm64 (default: amd64)"
    echo "  AKS_MIXED_NODE_KUBERNETES_VERSION (Optional) Mixed-profile Kubernetes version (default: 1.35)"
    exit 1
}

# Parse command-line arguments
IS_TEST=false
WHAT_IF=false
RESOURCE_PREFIX=""
readonly INTEGRATION_PREFIX_PATTERN='^aks-[a-z0-9]+(-[a-z0-9]+)*-[0-9]+-[0-9]+-[0-9]+$'
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --resource-group) AKS_RESOURCE_GROUP="$2"; shift ;;
        --location) AKS_LOCATION="$2"; shift ;;
        --template) AKS_TEMPLATE="$2"; shift ;;
        --is-test) IS_TEST="$2"; shift ;;
        --resource-prefix) RESOURCE_PREFIX="$2"; shift ;;
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
PARAM_FILE="${AKS_PARAMETER_FILE:-$DEPLOY_DIR/parameters/$AKS_TEMPLATE.json}"
MIXED_NODE_OS="${AKS_MIXED_NODE_OS:-false}"
readonly MIXED_NODE_OS="${MIXED_NODE_OS,,}"
readonly MIXED_NODE_VERSION="${AKS_MIXED_NODE_KUBERNETES_VERSION:-1.35}"
readonly NODE_ARCHITECTURE="${AKS_NODE_ARCHITECTURE:-amd64}"
if [[ "$NODE_ARCHITECTURE" != amd64 && "$NODE_ARCHITECTURE" != arm64 ]]; then
    echo "AKS_NODE_ARCHITECTURE must be amd64 or arm64." >&2
    exit 1
fi
if [[ "$NODE_ARCHITECTURE" == arm64 && -z "$RESOURCE_PREFIX" ]]; then
    echo "ARM64 node provisioning is supported only for integration runs." >&2
    exit 1
fi
if [[ "$MIXED_NODE_OS" != true && "$MIXED_NODE_OS" != false ]]; then
    echo "AKS_MIXED_NODE_OS must be true or false." >&2
    exit 1
fi
if [[ "$MIXED_NODE_OS" == true ]]; then
    if [[ -z "$RESOURCE_PREFIX" ]]; then
        echo "Mixed node OS provisioning is supported only for integration runs." >&2
        exit 1
    fi
    if [[ ! "$MIXED_NODE_VERSION" =~ ^1\.(34|35|36)(\.[0-9]+)?$ ]]; then
        echo "Mixed node OS coverage requires Kubernetes 1.34, 1.35, or 1.36." >&2
        exit 1
    fi
    echo "Using mixed Linux nodes ($NODE_ARCHITECTURE) on Kubernetes $MIXED_NODE_VERSION; the single-pool template count does not apply."
fi

if [[ "$AKS_RESOURCE_GROUP" == dacs-integration && -z "$RESOURCE_PREFIX" ]]; then
    echo "Shared resource group dacs-integration requires --resource-prefix; subscription deployment is not allowed." >&2
    exit 1
fi

if [[ -n "$RESOURCE_PREFIX" ]]; then
    if [[ "$AKS_RESOURCE_GROUP" != dacs-integration || "$IS_TEST" != true ||
          ! "$RESOURCE_PREFIX" =~ $INTEGRATION_PREFIX_PATTERN ]]; then
        echo "Integration provisioning requires dacs-integration, --is-test true, and an aks-<suite>-<build>-<stage-attempt>-<job-attempt> prefix." >&2
        exit 1
    fi
    PURPOSE="$(az group show --name "$AKS_RESOURCE_GROUP" --query tags.purpose --output tsv)"
    if [[ "$PURPOSE" != dacs-integration ]]; then
        echo "Resource group $AKS_RESOURCE_GROUP must have purpose=dacs-integration; it will not be created or retagged." >&2
        exit 1
    fi
    INTEGRATION_PARAMETERS="$(mktemp)"
    trap 'rm -f "$INTEGRATION_PARAMETERS"' EXIT
    jq --argjson mixed "$MIXED_NODE_OS" --arg version "$MIXED_NODE_VERSION" --arg architecture "$NODE_ARCHITECTURE" '
        .parameters |= with_entries(select(.key == "agentCount" or .key == "agentVMSize" or .key == "osSKU" or .key == "zones")) |
        .parameters.nodeArchitecture = {value: $architecture} |
        if $mixed then
            .parameters.mixedNodeOS = {value: true} |
            .parameters.mixedNodeKubernetesVersion = {value: $version}
        else . end' \
        "$PARAM_FILE" > "$INTEGRATION_PARAMETERS"
    if [[ "$WHAT_IF" == true ]]; then
        az deployment group what-if \
            --resource-group "$AKS_RESOURCE_GROUP" --name "$RESOURCE_PREFIX-deployment" \
            --template-file "$DEPLOY_DIR/template/integration.bicep" \
            --parameters "$INTEGRATION_PARAMETERS" resourcePrefix="$RESOURCE_PREFIX" location="$AKS_LOCATION"
        exit 0
    fi
    az deployment group create \
        --resource-group "$AKS_RESOURCE_GROUP" --name "$RESOURCE_PREFIX-deployment" \
        --template-file "$DEPLOY_DIR/template/integration.bicep" \
        --parameters "$INTEGRATION_PARAMETERS" resourcePrefix="$RESOURCE_PREFIX" location="$AKS_LOCATION" \
        --output none
elif [[ "$WHAT_IF" == "true" ]]; then
    # Run a what-if deployment and extract node configuration
    WHAT_IF_OUTPUT=$(az deployment sub what-if \
        --name "$AKS_RESOURCE_GROUP-deployment" \
        --location "$AKS_LOCATION" \
        --template-file "$DEPLOY_DIR/template/main.bicep" \
        --parameters "$PARAM_FILE" \
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
elif az aks show --resource-group "$AKS_RESOURCE_GROUP" --name "$AKS_RESOURCE_GROUP-cluster" > /dev/null 2>&1; then
    echo "AKS cluster already exists in resource group $AKS_RESOURCE_GROUP"
else
    # Create the AKS cluster using Bicep template
    az deployment sub create \
        --name "$AKS_RESOURCE_GROUP-deployment" \
        --location "$AKS_LOCATION" \
        --template-file "$DEPLOY_DIR/template/main.bicep" \
        --parameters "$PARAM_FILE" \
        --parameters resourceGroupName="$AKS_RESOURCE_GROUP" \
        --parameters isTest="$IS_TEST" \
        --what-if \
        --verbose

    az deployment sub create \
        --name "$AKS_RESOURCE_GROUP-deployment" \
        --location "$AKS_LOCATION" \
        --template-file "$DEPLOY_DIR/template/main.bicep" \
        --parameters "$PARAM_FILE" \
        --parameters resourceGroupName="$AKS_RESOURCE_GROUP" \
        --parameters isTest="$IS_TEST" \
        --verbose
fi

RESOURCE_PREFIX="${RESOURCE_PREFIX:-$AKS_RESOURCE_GROUP}"
CLUSTER="$(az aks show --resource-group "$AKS_RESOURCE_GROUP" --name "$RESOURCE_PREFIX-cluster" --output json)"
CONTROL_PLANE_ID="$(jq -er '.identity.principalId' <<< "$CLUSTER")"
VNET_ID="$(az network vnet show --resource-group "$AKS_RESOURCE_GROUP" --name "$RESOURCE_PREFIX-vnet" --query id --output tsv)"
echo "Granting AKS control-plane access to $VNET_ID..."
az role assignment create \
    --assignee-object-id "$CONTROL_PLANE_ID" \
    --assignee-principal-type ServicePrincipal \
    --role "Network Contributor" \
    --scope "${VNET_ID:?}" \
    --output none

# Grant image pulls to the kubelet without using --attach-acr, which does not support ABAC.
# Use DOCKER_REGISTRY env var if set and ends with azurecr.io, otherwise use default
DEFAULT_ACR="dacs"
if [[ -z "${DOCKER_REGISTRY:-}" || "$DOCKER_REGISTRY" == *".azurecr.io" ]]; then
    if [[ -n "${DOCKER_REGISTRY:-}" ]]; then
        ACR_NAME="${DOCKER_REGISTRY%.azurecr.io}"
    else
        ACR_NAME="$DEFAULT_ACR"
    fi

    ACR="$(az acr show --name "$ACR_NAME" --output json)"
    ACR_ID="$(jq -er '.id' <<< "$ACR")"
    ACR_MODE="$(jq -er '.roleAssignmentMode' <<< "$ACR")"
    case "$ACR_MODE" in
        AbacRepositoryPermissions) ACR_PULL_ROLE="Container Registry Repository Reader" ;;
        LegacyRegistryPermissions) ACR_PULL_ROLE="AcrPull" ;;
        *)
            echo "Unsupported ACR role assignment mode: $ACR_MODE" >&2
            exit 1
            ;;
    esac
    KUBELET_ID="$(jq -er '.identityProfile.kubeletidentity.objectId' <<< "$CLUSTER")"
    echo "Granting $ACR_PULL_ROLE on $ACR_NAME to the AKS kubelet..."
    az role assignment create \
        --assignee-object-id "$KUBELET_ID" \
        --assignee-principal-type ServicePrincipal \
        --role "$ACR_PULL_ROLE" \
        --scope "$ACR_ID" \
        --output none
else
    echo "Skipping ACR attach (DOCKER_REGISTRY=$DOCKER_REGISTRY is not an Azure Container Registry)"
fi

# Get AKS cluster credentials
az aks get-credentials \
    --resource-group "$AKS_RESOURCE_GROUP" \
    --name "$RESOURCE_PREFIX-cluster" \
    --overwrite-existing

# Apply custom Prometheus scrape settings if monitoring is enabled
if [[ "$AKS_RESOURCE_GROUP" != dacs-integration ]] && jq -e '.parameters.azureMonitorWorkspaceId.value // empty' "$PARAM_FILE" > /dev/null 2>&1; then
    echo "Applying ama-metrics settings ConfigMap..."
    kubectl apply -f "$DEPLOY_DIR/ama-metrics-settings-configmap.yaml"
fi

echo ""
echo "AKS cluster created successfully!"
echo ""
echo "Next steps:"
echo "  Install cache-server with NVMe prerequisites enabled:"
echo "     make deploy DOCKER_REGISTRY=\"${DOCKER_REGISTRY:-dacstest.azurecr.io}\" BUILD_NUMBER=\"${BUILD_NUMBER:-<build-number>}\""
