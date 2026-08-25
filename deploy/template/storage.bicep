// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

@description('The ID of the AKS subnet to allow access from.')
param aksSubnetId string

var blobDataContributorRoleDefinitionId = subscriptionResourceId(
  'Microsoft.Authorization/roleDefinitions',
  'ba92f5b4-2d11-453d-a403-e96b0029c9fe' // Storage Blob Data Contributor role Id
)

resource storageAccount 'Microsoft.Storage/storageAccounts@2023-01-01' = {
  name: storageAccountName
  location: resourceGroup().location
  tags: {
    purpose: 'dacs-test'
    managedBy: 'cache-server-infra'
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
    networkAcls: {
      defaultAction: 'Deny'
      bypass: 'AzureServices'
      virtualNetworkRules: [
        {
          id: aksSubnetId
          action: 'Allow'
        }
      ]
    }
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
