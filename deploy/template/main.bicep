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

@description('The resource ID of the Log Analytics workspace. If provided, Container Insights is enabled.')
param logAnalyticsWorkspaceId string = ''

@description('The location of the Log Analytics workspace. Required when logAnalyticsWorkspaceId is set.')
param logAnalyticsWorkspaceLocation string = ''

@description('The resource ID of the Azure Monitor workspace for Prometheus metrics. If provided, Managed Prometheus is enabled.')
param azureMonitorWorkspaceId string = ''

@description('The location of the Azure Monitor workspace. Required when azureMonitorWorkspaceId is set.')
param azureMonitorWorkspaceLocation string = ''

@description('Whether to create a storage account.')
param createStorageAccount bool = true

@description('Whether to set up workload identity for pods.')
param enableWorkloadIdentity bool = true

@description('The Kubernetes namespace for workload identity.')
param workloadIdentityNamespace string = 'cache-server'

@description('The Kubernetes service account name for workload identity.')
param workloadIdentityServiceAccount string = 'perf-client-sa'

@description('The tags for the resource group.')
param tags object = {}

@description('The current time in ISO 8601 format.')
param currentTimeISO string = utcNow('o')

// Combine tags with test-specific tags if isTest is true
var finalTags = union(tags, {
  managedBy: 'cache-server-infra'
}, isTest ? {
  createdBy: 'cache-server-tests'
  createdAt: currentTimeISO
} : {})


resource resourceGroup 'Microsoft.Resources/resourceGroups@2024-03-01' = {
  name: resourceGroupName
  location: deployment().location
  tags: finalTags
}

module cluster 'cluster.bicep' = {
  scope: resourceGroup
  params: {
    zones: zones
    agentCount: agentCount
    agentVMSize: agentVMSize
    osSKU: osSKU
    logAnalyticsWorkspaceId: logAnalyticsWorkspaceId
    logAnalyticsWorkspaceLocation: logAnalyticsWorkspaceLocation
    azureMonitorWorkspaceId: azureMonitorWorkspaceId
    azureMonitorWorkspaceLocation: azureMonitorWorkspaceLocation
  }
}

module storage 'storage.bicep' = if (createStorageAccount) {
  scope: resourceGroup
  params: {
    aksResourceGroup: resourceGroup.name
    aksName: cluster.outputs.name
    aksSubnetId: cluster.outputs.aksSubnetId
  }
}

module workloadIdentity 'workload-identity.bicep' = if (enableWorkloadIdentity && createStorageAccount) {
  scope: resourceGroup
  params: {
    aksClusterName: cluster.outputs.name
    storageAccountName: storage!.outputs.storageAccountName
    kubernetesNamespace: workloadIdentityNamespace
    serviceAccountName: workloadIdentityServiceAccount
  }
}

output managedClusterName string = cluster.outputs.name
output oidcIssuerUrl string = cluster.outputs.oidcIssuerUrl
output workloadIdentityClientId string = enableWorkloadIdentity && createStorageAccount ? workloadIdentity!.outputs.managedIdentityClientId : ''
