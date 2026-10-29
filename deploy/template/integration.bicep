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

targetScope = 'resourceGroup'

@description('Unique suite, build, stage-attempt, and job-attempt prefix.')
@minLength(1)
@maxLength(50)
param resourcePrefix string

@description('Region for this suite, independent of the shared resource group location.')
param location string

@description('The number of nodes for the nodepool.')
@minValue(1)
param agentCount int = 6

@description('The size of the Virtual Machine (NVMe).')
param agentVMSize string = 'Standard_L8s_v3'

@description('The SKU of the nodepool.')
param osSKU string = 'AzureLinux'

@description('Use mixed Linux nodes instead of the single-pool template count.')
param mixedNodeOS bool = false

@description('Node architecture. ARM64 uses Standard_L16ps_v5 local NVMe nodes.')
@allowed([
  'amd64'
  'arm64'
])
param nodeArchitecture string = 'amd64'

@description('Kubernetes version for mixed Linux integration clusters only.')
param mixedNodeKubernetesVersion string = '1.35'

@description('The zones in which the cluster should be created.')
param zones array = []

var tags = {
  createdBy: 'cache-server-tests'
  integrationRun: resourcePrefix
}

resource defenderWorkspace 'Microsoft.OperationalInsights/workspaces@2023-09-01' existing = {
  name: 'dacs-integration-defender'
}

module cluster 'cluster.bicep' = {
  name: '${resourcePrefix}-cluster'
  params: {
    resourcePrefix: resourcePrefix
    location: location
    tags: tags
    agentCount: agentCount
    agentVMSize: nodeArchitecture == 'arm64' ? 'Standard_L16ps_v5' : agentVMSize
    osSKU: osSKU
    mixedNodeOS: mixedNodeOS
    nodeArchitecture: nodeArchitecture
    mixedNodeKubernetesVersion: mixedNodeKubernetesVersion
    zones: zones
    defenderLogAnalyticsWorkspaceId: defenderWorkspace.id
    deployAzureSecurityLinuxAgent: false
  }
}

module storage 'storage.bicep' = {
  name: '${resourcePrefix}-storage'
  params: {
    storageAccountName: 'dacs${uniqueString(resourceGroup().id, resourcePrefix)}'
    location: location
    tags: tags
    aksName: cluster.outputs.name
    aksSubnetId: cluster.outputs.aksSubnetId
  }
}

module workloadIdentity 'workload-identity.bicep' = {
  name: '${resourcePrefix}-identity'
  params: {
    managedIdentityName: '${resourcePrefix}-identity'
    location: location
    tags: tags
    aksClusterName: cluster.outputs.name
    storageAccountName: storage.outputs.storageAccountName
  }
}

output storageAccountName string = storage.outputs.storageAccountName
output workloadIdentityClientId string = workloadIdentity.outputs.managedIdentityClientId
