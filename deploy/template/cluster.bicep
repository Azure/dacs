@description('The number of nodes for the nodepool.')
@minValue(1)
param agentCount int = 3

@description('The size of the Virtual Machine (NVMe).')
param agentVMSize string = 'standard_l8s_v3'

@description('The SKU of the nodepool.')
param osSKU string = 'AzureLinux'

@description('The zones in which the cluster should be created.')
param zones array = []

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
