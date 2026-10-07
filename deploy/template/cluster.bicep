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

@description('Prefix for cluster, VNet, and managed node resource group names.')
param resourcePrefix string = resourceGroup().name

@description('Location of the cluster and VNet.')
param location string = resourceGroup().location

@description('Additional resource ownership tags.')
param tags object = {}

@description('The number of nodes for the nodepool.')
@minValue(1)
param agentCount int = 3

@description('The size of the Virtual Machine (NVMe).')
param agentVMSize string = 'standard_l8s_v3'

@description('The SKU of the nodepool.')
param osSKU string = 'AzureLinux'

@description('The zones in which the cluster should be created.')
param zones array = []

@description('The address prefix for the virtual network.')
param vnetAddressPrefix string = '10.224.0.0/16'

@description('The address prefix for the AKS subnet.')
param aksSubnetAddressPrefix string = '10.224.0.0/20'

@description('The streams to collect and send to Log Analytics.')
param streams array = [
  'Microsoft-ContainerInsights-Group-Default'
]

@description('The resource ID of the Log Analytics workspace. If provided, Container Insights is enabled.')
param logAnalyticsWorkspaceId string = ''

@description('The location of the Log Analytics workspace.')
param logAnalyticsWorkspaceLocation string = ''

@description('The resource ID of the Log Analytics workspace for Microsoft Defender. If provided, Defender is enabled during cluster creation.')
param defenderLogAnalyticsWorkspaceId string = ''

@description('Deploy the Azure Security Linux Agent extension. Disable when Azure Policy manages the extension.')
param deployAzureSecurityLinuxAgent bool = true

@description('The resource ID of the Azure Monitor workspace for Prometheus metrics. If provided, Managed Prometheus is enabled.')
param azureMonitorWorkspaceId string = ''

@description('The location of the Azure Monitor workspace.')
param azureMonitorWorkspaceLocation string = ''

// Monitoring is implied by providing the resource IDs
var enableContainerInsights = logAnalyticsWorkspaceId != ''
var enableDefender = defenderLogAnalyticsWorkspaceId != ''
var enablePrometheus = azureMonitorWorkspaceId != ''

// Single NVMe pool that runs both system and cache-server pods
var nvmePool = {
  name: 'nvmepool'
  vmSize: agentVMSize
  osDiskSizeGB: 128
  type: 'VirtualMachineScaleSets'
  osType: 'Linux'
  osSKU: osSKU
  mode: 'System'
  maxPods: 125
  availabilityZones: zones
  count: agentCount
  vnetSubnetID: vnet.properties.subnets[0].id
  nodeLabels: {
    'dacs.azure.com/processing-unit': 'cpu'
  }
}

// Virtual network for AKS with storage service endpoint
resource vnet 'Microsoft.Network/virtualNetworks@2023-11-01' = {
  name: '${resourcePrefix}-vnet'
  location: location
  tags: union(tags, {
    managedBy: 'cache-server-infra'
  })
  properties: {
    addressSpace: {
      addressPrefixes: [
        vnetAddressPrefix
      ]
    }
    subnets: [
      {
        name: 'aks-subnet'
        properties: {
          addressPrefix: aksSubnetAddressPrefix
          serviceEndpoints: [
            {
              service: 'Microsoft.Storage'
            }
          ]
        }
      }
    ]
  }
}

resource aks 'Microsoft.ContainerService/managedClusters@2024-01-01' = {
  name: '${resourcePrefix}-cluster'
  location: location
  tags: union(tags, {
    managedBy: 'cache-server-infra'
  })
  sku: {
    name: 'Base'
    tier: 'Standard'
  }
  identity: {
    type: 'SystemAssigned'
  }
  properties: {
    nodeResourceGroup: '${resourcePrefix}-node-rg'
    dnsPrefix: '${resourcePrefix}-dns'
    // Enable OIDC issuer for workload identity
    oidcIssuerProfile: {
      enabled: true
    }
    // Enable workload identity
    securityProfile: union({
      workloadIdentity: {
        enabled: true
      }
    }, enableDefender ? {
      defender: {
        logAnalyticsWorkspaceResourceId: defenderLogAnalyticsWorkspaceId
        securityMonitoring: {
          enabled: true
        }
      }
    } : {})
    addonProfiles: enableContainerInsights ? {
      omsagent: {
        enabled: true
        config: {
          logAnalyticsWorkspaceResourceID: logAnalyticsWorkspaceId
          useAADAuth: 'true'
        }
      }
    } : {}
    agentPoolProfiles: [ nvmePool ]
    servicePrincipalProfile: {
      clientId: 'msi'
    }
    networkProfile: {
      networkPlugin: 'azure'
    }
    azureMonitorProfile: enablePrometheus ? {
      metrics: {
        enabled: true
      }
    } : {}
    storageProfile: {
      fileCSIDriver: {
        enabled: false
      }
      diskCSIDriver: {
        enabled: false
      }
      snapshotController: {
        enabled: false
      }
    }
  }
}

// Azure Security Linux Agent extension for Defender for Containers
resource azureSecurityLinuxAgent 'Microsoft.KubernetesConfiguration/extensions@2023-05-01' = if (deployAzureSecurityLinuxAgent) {
  name: 'azuresecuritylinuxagent'
  scope: aks
  properties: {
    extensionType: 'Microsoft.AzureSecurityLinuxAgent'
    autoUpgradeMinorVersion: true
    releaseTrain: 'stable'
    scope: {
      cluster: {
        releaseNamespace: 'azuresecuritylinuxagent'
      }
    }
  }
}

// Container Insights – logs to Log Analytics
module containerInsights './container-insights.bicep' = if (enableContainerInsights) {
  name: 'container-insights'
  params: {
    clusterName: aks.name
    logAnalyticsWorkspaceId: logAnalyticsWorkspaceId
    logAnalyticsWorkspaceLocation: logAnalyticsWorkspaceLocation
    streams: streams
  }
}

// Managed Prometheus – metrics to Azure Monitor Workspace
module prometheus './monitoring.bicep' = if (enablePrometheus) {
  name: 'prometheus'
  params: {
    clusterName: aks.name
    azureMonitorWorkspaceId: azureMonitorWorkspaceId
    azureMonitorWorkspaceLocation: azureMonitorWorkspaceLocation
  }
}

output name string = aks.name
output kubeletIdentity string = aks.properties.identityProfile.kubeletidentity.objectId
output oidcIssuerUrl string = aks.properties.oidcIssuerProfile.issuerURL
output aksSubnetId string = vnet.properties.subnets[0].id
