targetScope='subscription'

@description('The resource group of the Managed Cluster resource.')
param resourceGroupName string

@description('The number of nodes for the nodepool.')
@minValue(1)
param agentCount int = 3

@description('The size of the Virtual Machine (NVMe).')
param agentVMSize string = 'standard_l8s_v3'

@description('The SKU of the nodepool.')
param osSKU string = 'AzureLinux'

@description('The zones in which the cluster should be created.')
param zones array = []

@description('Is test resource.')
param isTest bool = false

@description('Whether to create a storage account.')
param createStorageAccount bool = true

@description('The tags for the resource group.')
param tags object = {}

@description('The current time in ISO 8601 format.')
param currentTimeISO string = utcNow('o')

// Combine tags with test-specific tags if isTest is true
var finalTags = union(tags, isTest ? {
  createdBy: 'cache-server-tests'
  createdAt: currentTimeISO
} : {})


resource resourceGroup 'Microsoft.Resources/resourceGroups@2024-03-01' = {
  name: resourceGroupName
  location: deployment().location
  tags: finalTags
}

module cluster 'cluster.bicep' = {
  name: 'clusterModule'
  scope: resourceGroup
  params: {
    zones: zones
    agentCount: agentCount
    agentVMSize: agentVMSize
    osSKU: osSKU
  }
}

module storage 'storage.bicep' = if (createStorageAccount) {
  name: 'storageModule'
  scope: resourceGroup
}

output managedClusterName string = cluster.outputs.name
