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
