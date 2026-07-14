// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

@description('The name of the AKS cluster to monitor.')
param clusterName string

@description('The resource ID of the Log Analytics workspace.')
param logAnalyticsWorkspaceId string

@description('The location of the Log Analytics workspace. Must match the workspace to avoid cross-region DCR errors.')
param logAnalyticsWorkspaceLocation string

@description('The streams to collect and send to Log Analytics.')
param streams array

// Reference the existing AKS cluster for DCRA scoping
resource aks 'Microsoft.ContainerService/managedClusters@2024-01-01' existing = {
  name: clusterName
}

// Data collection rule for Container Insights logs
resource dataCollectionRule 'Microsoft.Insights/dataCollectionRules@2022-06-01' = {
  name: '${resourceGroup().name}-dcr'
  location: logAnalyticsWorkspaceLocation
  tags: {
    managedBy: 'cache-server-infra'
  }
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

// Associate the Container Insights DCR with the AKS cluster
resource dataCollectionRuleAssociation 'Microsoft.Insights/dataCollectionRuleAssociations@2022-06-01' = {
  name: 'ContainerInsightsExtension'
  scope: aks
  properties: {
    description: 'Association of data collection rule. Deleting this association will break the data collection for this AKS Cluster.'
    dataCollectionRuleId: dataCollectionRule.id
  }
}
