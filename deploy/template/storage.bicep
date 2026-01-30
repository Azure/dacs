// Storage account for test artifacts
// Storage account names: 3-24 chars, lowercase alphanumeric only
@minLength(3)
@maxLength(24)
param storageAccountName string = toLower(take('${replace(resourceGroup().name, '-', '')}storage', 24))

// AKS Cluster name
@description('AKS Cluster Name')
param aksName string

@description('AKS Resource Group')
param aksResourceGroup string = resourceGroup().name

var blobDataContributorRoleDefinitionId = subscriptionResourceId(
  'Microsoft.Authorization/roleDefinitions',
  '00000000-0000-0000-0000-000000000000' // Storage Blob Data Contributor role Id
)

resource storageAccount 'Microsoft.Storage/storageAccounts@2023-01-01' = {
  name: storageAccountName
  location: resourceGroup().location
  tags: {
    purpose: 'dacs-test'
  }
  sku: {
    name: 'Standard_LRS'
  }
  kind: 'StorageV2'
  properties: {
    accessTier: 'Hot'
    minimumTlsVersion: 'TLS1_2'
    supportsHttpsTrafficOnly: true
    allowBlobPublicAccess: false
  }
}

resource blobService 'Microsoft.Storage/storageAccounts/blobServices@2023-01-01' = {
  parent: storageAccount
  name: 'default'
}

resource testContainer 'Microsoft.Storage/storageAccounts/blobServices/containers@2023-01-01' = {
  parent: blobService
  name: 'test-data'
  properties: {
    publicAccess: 'None'
  }
}

resource aks 'Microsoft.ContainerService/managedClusters@2023-08-01' existing = {
  name: aksName
  scope: resourceGroup(aksResourceGroup)
}

resource kubeletRoleAssignment 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(storageAccount.id, aks.id, blobDataContributorRoleDefinitionId)
  scope: storageAccount
  properties: {
    roleDefinitionId: blobDataContributorRoleDefinitionId
    principalId: aks.properties.identityProfile.kubeletidentity.objectId
    principalType: 'ServicePrincipal'
  }
}

output storageAccountName string = storageAccount.name
output storageContainerName string = testContainer.name