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
    echo "Usage: $0 --resource-group <AKS_RESOURCE_GROUP>"
    echo "  --resource-group  AKS resource group name"
    echo "  --resource-prefix (Optional) Delete only this integration suite/run, never its resource group"
    exit 1
}

# Parse command-line arguments
RESOURCE_PREFIX=""
readonly INTEGRATION_PREFIX_PATTERN='^aks-[a-z0-9]+(-[a-z0-9]+)*-[0-9]+-[0-9]+-[0-9]+$'
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --resource-group) AKS_RESOURCE_GROUP="$2"; shift ;;
        --resource-prefix) RESOURCE_PREFIX="$2"; shift ;;
        *) usage ;;
    esac
    shift
done

# Ensure required arguments are provided and not just whitespace
if [[ -z "${AKS_RESOURCE_GROUP:-}" || "$AKS_RESOURCE_GROUP" =~ ^[[:space:]]*$ ]]; then
    echo "Error: AKS_RESOURCE_GROUP cannot be empty or just whitespace."
    usage
fi

if [[ "$AKS_RESOURCE_GROUP" == dacs-integration || -n "$RESOURCE_PREFIX" ]]; then
    if [[ "$AKS_RESOURCE_GROUP" != dacs-integration ||
          ! "$RESOURCE_PREFIX" =~ $INTEGRATION_PREFIX_PATTERN ]]; then
        echo "Refusing cleanup: dacs-integration requires an explicit aks-<suite>-<build>-<stage-attempt>-<job-attempt> prefix." >&2
        exit 1
    fi
    RG_EXISTS="$(az group exists --name "$AKS_RESOURCE_GROUP")"
    if [[ "$RG_EXISTS" == false ]]; then
        echo "Resource group $AKS_RESOURCE_GROUP does not exist. Nothing to clean up."
        exit 0
    fi
    PURPOSE="$(az group show --name "$AKS_RESOURCE_GROUP" --query tags.purpose --output tsv)"
    if [[ "$PURPOSE" != dacs-integration ]]; then
        echo "Refusing cleanup: resource group must have purpose=dacs-integration." >&2
        exit 1
    fi
    RESOURCES="$(az resource list --resource-group "$AKS_RESOURCE_GROUP" --output json)"
    OWNED="$(jq --arg prefix "$RESOURCE_PREFIX" '
        [.[] | select(.tags.integrationRun == $prefix)]
    ' <<< "$RESOURCES")"
    if ! jq -e --arg prefix "$RESOURCE_PREFIX" '
        all(.[];
            .tags.integrationRun == $prefix and
            .tags.createdBy == "cache-server-tests" and
            .tags.managedBy == "cache-server-infra" and
            ((.type == "Microsoft.ContainerService/managedClusters" and .name == ($prefix + "-cluster")) or
             (.type == "Microsoft.Network/virtualNetworks" and .name == ($prefix + "-vnet")) or
             (.type == "Microsoft.ManagedIdentity/userAssignedIdentities" and .name == ($prefix + "-identity")) or
             .type == "Microsoft.Storage/storageAccounts")
        )
    ' <<< "$OWNED" > /dev/null; then
        echo "Refusing cleanup: resource ownership tags or resource types do not match $RESOURCE_PREFIX." >&2
        exit 1
    fi
    CLUSTER_ID="$(jq -r '.[] | select(.type == "Microsoft.ContainerService/managedClusters") | .id' <<< "$OWNED")"
    if [[ -n "$CLUSTER_ID" ]]; then
        CLUSTER="$(az aks show --resource-group "$AKS_RESOURCE_GROUP" --name "$RESOURCE_PREFIX-cluster" --output json)"
        KUBELET_ID="$(jq -r '.identityProfile.kubeletidentity.objectId // empty' <<< "$CLUSTER")"
        if [[ -n "$KUBELET_ID" && "${DOCKER_REGISTRY:-}" == *".azurecr.io" ]]; then
            ACR_ID="$(az acr show --name "${DOCKER_REGISTRY%.azurecr.io}" --query id --output tsv)"
            ASSIGNMENTS="$(az role assignment list --scope "${ACR_ID:?}" \
                --fill-principal-name false --fill-role-definition-name false --output json)"
            ASSIGNMENT_IDS="$(jq -r --arg principal "$KUBELET_ID" --arg scope "$ACR_ID" '
                .[] | select(.principalId == $principal and (.scope | ascii_downcase) == ($scope | ascii_downcase)) |
                select((.roleDefinitionId | split("/")[-1]) == "b93aa761-3e63-49ed-ac28-beffa264f7ac" or
                       (.roleDefinitionId | split("/")[-1]) == "7f951dda-4ed3-4680-a7ca-43fe172d538d") | .id
            ' <<< "$ASSIGNMENTS")"
            if [[ -n "$ASSIGNMENT_IDS" ]]; then
                while IFS= read -r assignment; do
                    az role assignment delete --ids "$assignment"
                done <<< "$ASSIGNMENT_IDS"
            fi
        fi
        echo "Deleting $RESOURCE_PREFIX-cluster and waiting for AKS-managed node cleanup..."
        az aks delete --resource-group "$AKS_RESOURCE_GROUP" --name "$RESOURCE_PREFIX-cluster" --yes
    fi
    RESOURCE_IDS="$(jq -r '.[] | select(.type != "Microsoft.ContainerService/managedClusters") | .id' <<< "$OWNED")"
    if [[ -n "$RESOURCE_IDS" ]]; then
        while IFS= read -r resource; do
            echo "Deleting owned resource $resource..."
            az resource delete --ids "$resource"
        done <<< "$RESOURCE_IDS"
    fi
    NRMS_NSG_IDS="$(az network nsg list --resource-group "$AKS_RESOURCE_GROUP" --output json |
        jq -r --arg vnet "$RESOURCE_PREFIX-vnet" '
            .[] | select(
                (.name | startswith("NRMS-")) and
                (.name | endswith($vnet)) and
                (((.subnets // []) | length) == 0) and
                (((.networkInterfaces // []) | length) == 0)
            ) | .id
        ')"
    if [[ -n "$NRMS_NSG_IDS" ]]; then
        while IFS= read -r nsg; do
            echo "Deleting policy-created network security group $nsg..."
            az network nsg delete --ids "$nsg"
        done <<< "$NRMS_NSG_IDS"
    fi
    echo "Cleanup completed for $RESOURCE_PREFIX. Resource group $AKS_RESOURCE_GROUP was preserved."
    exit 0
fi

# Check if the AKS cluster exists
if az aks show --resource-group "$AKS_RESOURCE_GROUP" --name "${AKS_RESOURCE_GROUP}-cluster" > /dev/null 2>&1; then
    echo "Deleting AKS cluster ${AKS_RESOURCE_GROUP}-cluster in resource group $AKS_RESOURCE_GROUP"
    az aks delete --name "${AKS_RESOURCE_GROUP}-cluster" --resource-group "$AKS_RESOURCE_GROUP" --yes --no-wait
    echo "Marked AKS cluster for deletion."
else
    echo "AKS cluster ${AKS_RESOURCE_GROUP}-cluster does not exist in resource group $AKS_RESOURCE_GROUP"
fi

# Check if the resource group exists
if [ "$(az group exists --name "$AKS_RESOURCE_GROUP")" = "false" ]; then
    echo "Resource group $AKS_RESOURCE_GROUP does not exist. Nothing to clean up."
    exit 0
fi

# Check if the resource group is empty or only has resources we created
# All resources created by our Bicep templates are tagged with managedBy=cache-server-infra
unmanaged_resources=$(az resource list --resource-group "$AKS_RESOURCE_GROUP" \
    --query "[?tags.managedBy != 'cache-server-infra' && !starts_with(name, 'NRMS-')].name" \
    --output tsv)
if [[ -n "$unmanaged_resources" ]]; then
    echo "Resource group $AKS_RESOURCE_GROUP contains unmanaged resources: $unmanaged_resources"
    echo "Skipping resource group deletion as a safety measure."
    exit 0
fi

echo "Resource group $AKS_RESOURCE_GROUP is empty. Deleting resource group."
az group delete --name "$AKS_RESOURCE_GROUP" --yes --no-wait

echo "Resource group $AKS_RESOURCE_GROUP marked for deletion."
