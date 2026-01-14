#!/usr/bin/env bash

set -euo pipefail

# Function to display usage information
usage() {
    echo "Usage: $0 --resource-group <AKS_RESOURCE_GROUP>"
    echo "  --resource-group  AKS resource group name"
    exit 1
}

# Parse command-line arguments
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --resource-group) AKS_RESOURCE_GROUP="$2"; shift ;;
        *) usage ;;
    esac
    shift
done

# Ensure required arguments are provided and not just whitespace
if [[ -z "${AKS_RESOURCE_GROUP:-}" || "$AKS_RESOURCE_GROUP" =~ ^[[:space:]]*$ ]]; then
    echo "Error: AKS_RESOURCE_GROUP cannot be empty or just whitespace."
    usage
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

# Check if the resource group is empty or just has expected resources (cluster + storage account)
# Storage account name formula matches storage.bicep: toLower(take('${replace(resourceGroup().name, '-', '')}storage', 24))
storage_account_name=$(echo "${AKS_RESOURCE_GROUP}storage" | tr -d '-' | tr '[:upper:]' '[:lower:]' | cut -c1-24)
resources=$(az resource list --resource-group "$AKS_RESOURCE_GROUP" --query "[?name!='${AKS_RESOURCE_GROUP}-cluster' && name!='${storage_account_name}'].name" --output tsv)
if [[ -n "$resources" ]]; then
    echo "Resource group $AKS_RESOURCE_GROUP is not empty. Resources found: $resources."
    echo "Skipping resource group deletion as a safety measure."
    exit 0
fi

echo "Resource group $AKS_RESOURCE_GROUP is empty. Deleting resource group."
az group delete --name "$AKS_RESOURCE_GROUP" --yes --no-wait

echo "Resource group $AKS_RESOURCE_GROUP marked for deletion."
