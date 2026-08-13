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

@description('The name of the AKS cluster')
param aksClusterName string

@description('The name of the user-assigned managed identity for workload identity')
param managedIdentityName string = 'perf-client-identity'

@description('The Kubernetes namespace where the service account will be created')
param kubernetesNamespace string = 'cache-server'

@description('The Kubernetes service account name that will use workload identity')
param serviceAccountName string = 'perf-client-sa'

@description('The name of the storage account to grant access to')
param storageAccountName string

// Reference the existing AKS cluster
resource aksCluster 'Microsoft.ContainerService/managedClusters@2024-01-01' existing = {
  name: aksClusterName
}

// Reference the existing storage account
resource storageAccount 'Microsoft.Storage/storageAccounts@2023-01-01' existing = {
  name: storageAccountName
}

// Create a user-assigned managed identity for the workload
resource managedIdentity 'Microsoft.ManagedIdentity/userAssignedIdentities@2023-01-31' = {
  name: managedIdentityName
  location: resourceGroup().location
  tags: {
    purpose: 'workload-identity'
    aksCluster: aksClusterName
    managedBy: 'cache-server-infra'
  }
}

// Create the federated identity credential to link Kubernetes ServiceAccount to Azure identity
// This allows pods using the ServiceAccount to authenticate as the managed identity
resource federatedCredential 'Microsoft.ManagedIdentity/userAssignedIdentities/federatedIdentityCredentials@2023-01-31' = {
  parent: managedIdentity
  name: '${kubernetesNamespace}-${serviceAccountName}'
  properties: {
    // The OIDC issuer URL from the AKS cluster
    issuer: aksCluster.properties.oidcIssuerProfile.issuerURL
    // The subject is the Kubernetes ServiceAccount in the format: system:serviceaccount:<namespace>:<serviceAccountName>
    subject: 'system:serviceaccount:${kubernetesNamespace}:${serviceAccountName}'
    // The audience for the token - Azure AD
    audiences: [
      'api://AzureADTokenExchange'
    ]
  }
}

// Assign Storage Blob Data Contributor role to the managed identity on the storage account
// This allows the identity to read/write blobs in the storage account
@description('Storage Blob Data Contributor role definition ID')
var storageBlobDataContributorRoleId = '00000000-0000-0000-0000-000000000000'

resource storageRoleAssignment 'Microsoft.Authorization/roleAssignments@2022-04-01' = {
  name: guid(storageAccount.id, managedIdentity.id, storageBlobDataContributorRoleId)
  scope: storageAccount
  properties: {
    roleDefinitionId: subscriptionResourceId('Microsoft.Authorization/roleDefinitions', storageBlobDataContributorRoleId)
    principalId: managedIdentity.properties.principalId
    principalType: 'ServicePrincipal'
  }
}

// Outputs needed for Kubernetes configuration
output managedIdentityClientId string = managedIdentity.properties.clientId
output managedIdentityPrincipalId string = managedIdentity.properties.principalId
output managedIdentityResourceId string = managedIdentity.id
output federatedCredentialName string = federatedCredential.name
output oidcIssuerUrl string = aksCluster.properties.oidcIssuerProfile.issuerURL
