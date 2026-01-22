@description('The number of nodes for the nodepool.')
@minValue(1)
param agentCount int = 3

@description('The size of the Virtual Machine (NVMe).')
param agentVMSize string = 'standard_l8s_v3'

@description('The SKU of the nodepool.')
param osSKU string = 'AzureLinux'

@description('The zones in which the cluster should be created.')
param zones array = []

@description('Enable monitoring for test clusters.')
param enableMonitoring bool = false

@description('The streams to collect and send to Log Analytics.')
param streams array = [
  'Microsoft-ContainerInsights-Group-Default'
]

@description('The resource ID of the Log Analytics workspace.')
param logAnalyticsWorkspaceId string = '/subscriptions/00000000-0000-0000-0000-000000000000/resourcegroups/storage_dacs_rg/providers/microsoft.operationalinsights/workspaces/dacs-analytics'

@description('The location of the Log Analytics workspace.')
param logAnalyticsWorkspaceLocation string = 'westus2'

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
  nodeLabels: {
    'dacs.azure.com/processing-unit': 'cpu'
    'dacs.azure.com/processing-unit': 'system'
  }
}

// Create the data collection rule and associate it with the AKS cluster.
// The data collection rule collects the specified streams and sends them to the Log Analytics workspace.
resource dataCollectionRule 'Microsoft.Insights/dataCollectionRules@2022-06-01' = if (enableMonitoring) {
  name: '${resourceGroup().name}-dcr'
  location: logAnalyticsWorkspaceLocation
  kind: 'Linux'
  properties: {
    dataSources: {
      extensions: [
        {
          name: 'ContainerInsightsExtension'
          streams: streams
          extensionSettings: {
            dataCollectionSettings: {
              enableContainerLogV2: true
              namespaceFilteringMode: 'Include'
              namespaces: [
                'cache-server'
                'default'
              ]
            }
          }
          extensionName: 'ContainerInsights'
        }
      ]
    }
    destinations: {
      logAnalytics: [
        {
          workspaceResourceId: logAnalyticsWorkspaceId
          name: 'la-workspace'
        }
      ]
    }
    dataFlows: [
      {
        streams: streams
        destinations: [
          'la-workspace'
        ]
      }
    ]
  }
}

// Associate the data collection rule with the AKS cluster.
resource dataCollectionRuleAssociation 'Microsoft.Insights/dataCollectionRuleAssociations@2022-06-01' = if (enableMonitoring) {
  name: 'ContainerInsightsExtension'
  scope: aks
  properties: {
    description: 'Association of data collection rule. Deleting this association will break the data collection for this AKS Cluster.'
    dataCollectionRuleId: dataCollectionRule.id
  }
}

resource aks 'Microsoft.ContainerService/managedClusters@2023-08-01' = {
  name: '${resourceGroup().name}-cluster'
  location: resourceGroup().location
  sku: {
    name: 'Base'
    tier: 'Standard'
  }
  identity: {
    type: 'SystemAssigned'
  }
  properties: {
    nodeResourceGroup: '${resourceGroup().name}-node-rg'
    dnsPrefix: '${resourceGroup().name}-dns'
    addonProfiles: enableMonitoring ? {
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

output name string = aks.name
output kubeletIdentity string = aks.properties.identityProfile.kubeletidentity.objectId
