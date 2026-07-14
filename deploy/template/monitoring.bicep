// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

// Adapted from Azure/prometheus-collector's standard Bicep template for Managed Prometheus on AKS.
// https://github.com/Azure/prometheus-collector/blob/main/AddonBicepTemplate/FullAzureMonitorMetricsProfile.bicep

@description('The name of the AKS cluster to monitor.')
param clusterName string

@description('The resource ID of the Azure Monitor workspace for Prometheus metrics.')
param azureMonitorWorkspaceId string

@description('The location of the Azure Monitor workspace. Must match the workspace to avoid cross-region DCR errors.')
param azureMonitorWorkspaceLocation string

// Reference the existing AKS cluster for DCRA scoping
resource aks 'Microsoft.ContainerService/managedClusters@2024-01-01' existing = {
  name: clusterName
}

// Prometheus metrics pipeline (Azure Monitor Workspace)

var promDceName = substring('MSProm-${azureMonitorWorkspaceLocation}-${clusterName}', 0, min(44, length('MSProm-${azureMonitorWorkspaceLocation}-${clusterName}')))
var promDcrName = substring('MSProm-${azureMonitorWorkspaceLocation}-${clusterName}', 0, min(64, length('MSProm-${azureMonitorWorkspaceLocation}-${clusterName}')))
var promDcraName = 'MSProm-${azureMonitorWorkspaceLocation}-${clusterName}'

// Data collection endpoint for Prometheus metrics ingestion
resource promDataCollectionEndpoint 'Microsoft.Insights/dataCollectionEndpoints@2022-06-01' = {
  name: promDceName
  location: azureMonitorWorkspaceLocation
  tags: {
    managedBy: 'cache-server-infra'
  }
  kind: 'Linux'
  properties: {}
}

// Data collection rule for Prometheus metrics (sends to Azure Monitor Workspace)
resource promDataCollectionRule 'Microsoft.Insights/dataCollectionRules@2022-06-01' = {
  name: promDcrName
  location: azureMonitorWorkspaceLocation
  tags: {
    managedBy: 'cache-server-infra'
  }
  kind: 'Linux'
  properties: {
    dataCollectionEndpointId: promDataCollectionEndpoint.id
    dataFlows: [
      {
        destinations: [
          'MonitoringAccount1'
        ]
        streams: [
          'Microsoft-PrometheusMetrics'
        ]
      }
    ]
    dataSources: {
      prometheusForwarder: [
        {
          name: 'PrometheusDataSource'
          streams: [
            'Microsoft-PrometheusMetrics'
          ]
          labelIncludeFilter: {}
        }
      ]
    }
    description: 'DCR for Azure Monitor Metrics Profile (Managed Prometheus)'
    destinations: {
      monitoringAccounts: [
        {
          accountResourceId: azureMonitorWorkspaceId
          name: 'MonitoringAccount1'
        }
      ]
    }
  }
}

// Associate the Prometheus DCR with the AKS cluster
resource promDataCollectionRuleAssociation 'Microsoft.Insights/dataCollectionRuleAssociations@2022-06-01' = {
  name: promDcraName
  scope: aks
  properties: {
    description: 'Association of Prometheus data collection rule. Deleting this association will break Prometheus metrics collection for this AKS Cluster.'
    dataCollectionRuleId: promDataCollectionRule.id
  }
}

// Prometheus recording rule groups

var nodeRecordingRuleGroupName = 'NodeRecordingRulesRuleGroup-${clusterName}'
var kubernetesRecordingRuleGroupName = 'KubernetesRecordingRulesRuleGroup-${clusterName}'
var uxRecordingRulesRuleGroupName = 'UXRecordingRulesRuleGroup-${clusterName}'

// Node Recording Rules - pre-aggregated node-level CPU, memory, disk, and network metrics
resource nodeRecordingRuleGroup 'Microsoft.AlertsManagement/prometheusRuleGroups@2023-03-01' = {
  name: nodeRecordingRuleGroupName
  location: azureMonitorWorkspaceLocation
  tags: {
    managedBy: 'cache-server-infra'
  }
  properties: {
    description: 'Node Recording Rules RuleGroup'
    scopes: [
      azureMonitorWorkspaceId
      aks.id
    ]
    enabled: true
    clusterName: clusterName
    interval: 'PT1M'
    rules: [
      {
        record: 'instance:node_num_cpu:sum'
        expression: 'count without (cpu, mode) (  node_cpu_seconds_total{job="node",mode="idle"})'
      }
      {
        record: 'instance:node_cpu_utilisation:rate5m'
        expression: '1 - avg without (cpu) (  sum without (mode) (rate(node_cpu_seconds_total{job="node", mode=~"idle|iowait|steal"}[5m])))'
      }
      {
        record: 'instance:node_load1_per_cpu:ratio'
        expression: '(  node_load1{job="node"}/  instance:node_num_cpu:sum{job="node"})'
      }
      {
        record: 'instance:node_memory_utilisation:ratio'
        expression: '1 - (  (    node_memory_MemAvailable_bytes{job="node"}    or    (      node_memory_Buffers_bytes{job="node"}      +      node_memory_Cached_bytes{job="node"}      +      node_memory_MemFree_bytes{job="node"}      +      node_memory_Slab_bytes{job="node"}    )  )/  node_memory_MemTotal_bytes{job="node"})'
      }
      {
        record: 'instance:node_vmstat_pgmajfault:rate5m'
        expression: 'rate(node_vmstat_pgmajfault{job="node"}[5m])'
      }
      {
        record: 'instance_device:node_disk_io_time_seconds:rate5m'
        expression: 'rate(node_disk_io_time_seconds_total{job="node", device!=""}[5m])'
      }
      {
        record: 'instance_device:node_disk_io_time_weighted_seconds:rate5m'
        expression: 'rate(node_disk_io_time_weighted_seconds_total{job="node", device!=""}[5m])'
      }
      {
        record: 'instance:node_network_receive_bytes_excluding_lo:rate5m'
        expression: 'sum without (device) (  rate(node_network_receive_bytes_total{job="node", device!="lo"}[5m]))'
      }
      {
        record: 'instance:node_network_transmit_bytes_excluding_lo:rate5m'
        expression: 'sum without (device) (  rate(node_network_transmit_bytes_total{job="node", device!="lo"}[5m]))'
      }
      {
        record: 'instance:node_network_receive_drop_excluding_lo:rate5m'
        expression: 'sum without (device) (  rate(node_network_receive_drop_total{job="node", device!="lo"}[5m]))'
      }
      {
        record: 'instance:node_network_transmit_drop_excluding_lo:rate5m'
        expression: 'sum without (device) (  rate(node_network_transmit_drop_total{job="node", device!="lo"}[5m]))'
      }
    ]
  }
}

// Kubernetes Recording Rules - pre-aggregated container CPU/memory, resource requests/limits, workload ownership
resource kubernetesRecordingRuleGroup 'Microsoft.AlertsManagement/prometheusRuleGroups@2023-03-01' = {
  name: kubernetesRecordingRuleGroupName
  location: azureMonitorWorkspaceLocation
  tags: {
    managedBy: 'cache-server-infra'
  }
  properties: {
    description: 'Kubernetes Recording Rules RuleGroup'
    scopes: [
      azureMonitorWorkspaceId
      aks.id
    ]
    enabled: true
    clusterName: clusterName
    interval: 'PT1M'
    rules: [
      {
        record: 'node_namespace_pod_container:container_cpu_usage_seconds_total:sum_irate'
        expression: 'sum by (cluster, namespace, pod, container) (  irate(container_cpu_usage_seconds_total{job="cadvisor", image!=""}[5m])) * on (cluster, namespace, pod) group_left(node) topk by (cluster, namespace, pod) (  1, max by(cluster, namespace, pod, node) (kube_pod_info{node!=""}))'
      }
      {
        record: 'node_namespace_pod_container:container_memory_working_set_bytes'
        expression: 'container_memory_working_set_bytes{job="cadvisor", image!=""}* on (namespace, pod) group_left(node) topk by(namespace, pod) (1,  max by(namespace, pod, node) (kube_pod_info{node!=""}))'
      }
      {
        record: 'node_namespace_pod_container:container_memory_rss'
        expression: 'container_memory_rss{job="cadvisor", image!=""}* on (namespace, pod) group_left(node) topk by(namespace, pod) (1,  max by(namespace, pod, node) (kube_pod_info{node!=""}))'
      }
      {
        record: 'node_namespace_pod_container:container_memory_cache'
        expression: 'container_memory_cache{job="cadvisor", image!=""}* on (namespace, pod) group_left(node) topk by(namespace, pod) (1,  max by(namespace, pod, node) (kube_pod_info{node!=""}))'
      }
      {
        record: 'node_namespace_pod_container:container_memory_swap'
        expression: 'container_memory_swap{job="cadvisor", image!=""}* on (namespace, pod) group_left(node) topk by(namespace, pod) (1,  max by(namespace, pod, node) (kube_pod_info{node!=""}))'
      }
      {
        record: 'cluster:namespace:pod_memory:active:kube_pod_container_resource_requests'
        expression: 'kube_pod_container_resource_requests{resource="memory",job="kube-state-metrics"}  * on (namespace, pod, cluster)group_left() max by (namespace, pod, cluster) (  (kube_pod_status_phase{phase=~"Pending|Running"} == 1))'
      }
      {
        record: 'namespace_memory:kube_pod_container_resource_requests:sum'
        expression: 'sum by (namespace, cluster) (    sum by (namespace, pod, cluster) (        max by (namespace, pod, container, cluster) (          kube_pod_container_resource_requests{resource="memory",job="kube-state-metrics"}        ) * on(namespace, pod, cluster) group_left() max by (namespace, pod, cluster) (          kube_pod_status_phase{phase=~"Pending|Running"} == 1        )    ))'
      }
      {
        record: 'cluster:namespace:pod_cpu:active:kube_pod_container_resource_requests'
        expression: 'kube_pod_container_resource_requests{resource="cpu",job="kube-state-metrics"}  * on (namespace, pod, cluster)group_left() max by (namespace, pod, cluster) (  (kube_pod_status_phase{phase=~"Pending|Running"} == 1))'
      }
      {
        record: 'namespace_cpu:kube_pod_container_resource_requests:sum'
        expression: 'sum by (namespace, cluster) (    sum by (namespace, pod, cluster) (        max by (namespace, pod, container, cluster) (          kube_pod_container_resource_requests{resource="cpu",job="kube-state-metrics"}        ) * on(namespace, pod, cluster) group_left() max by (namespace, pod, cluster) (          kube_pod_status_phase{phase=~"Pending|Running"} == 1        )    ))'
      }
      {
        record: 'cluster:namespace:pod_memory:active:kube_pod_container_resource_limits'
        expression: 'kube_pod_container_resource_limits{resource="memory",job="kube-state-metrics"}  * on (namespace, pod, cluster)group_left() max by (namespace, pod, cluster) (  (kube_pod_status_phase{phase=~"Pending|Running"} == 1))'
      }
      {
        record: 'namespace_memory:kube_pod_container_resource_limits:sum'
        expression: 'sum by (namespace, cluster) (    sum by (namespace, pod, cluster) (        max by (namespace, pod, container, cluster) (          kube_pod_container_resource_limits{resource="memory",job="kube-state-metrics"}        ) * on(namespace, pod, cluster) group_left() max by (namespace, pod, cluster) (          kube_pod_status_phase{phase=~"Pending|Running"} == 1        )    ))'
      }
      {
        record: 'cluster:namespace:pod_cpu:active:kube_pod_container_resource_limits'
        expression: 'kube_pod_container_resource_limits{resource="cpu",job="kube-state-metrics"}  * on (namespace, pod, cluster)group_left() max by (namespace, pod, cluster) ( (kube_pod_status_phase{phase=~"Pending|Running"} == 1) )'
      }
      {
        record: 'namespace_cpu:kube_pod_container_resource_limits:sum'
        expression: 'sum by (namespace, cluster) (    sum by (namespace, pod, cluster) (        max by (namespace, pod, container, cluster) (          kube_pod_container_resource_limits{resource="cpu",job="kube-state-metrics"}        ) * on(namespace, pod, cluster) group_left() max by (namespace, pod, cluster) (          kube_pod_status_phase{phase=~"Pending|Running"} == 1        )    ))'
      }
      {
        record: 'namespace_workload_pod:kube_pod_owner:relabel'
        expression: 'max by (cluster, namespace, workload, pod) (  label_replace(    label_replace(      kube_pod_owner{job="kube-state-metrics", owner_kind="ReplicaSet"},      "replicaset", "$1", "owner_name", "(.*)"    ) * on(replicaset, namespace) group_left(owner_name) topk by(replicaset, namespace) (      1, max by (replicaset, namespace, owner_name) (        kube_replicaset_owner{job="kube-state-metrics"}      )    ),    "workload", "$1", "owner_name", "(.*)"  ))'
        labels: {
          workload_type: 'deployment'
        }
      }
      {
        record: 'namespace_workload_pod:kube_pod_owner:relabel'
        expression: 'max by (cluster, namespace, workload, pod) (  label_replace(    kube_pod_owner{job="kube-state-metrics", owner_kind="DaemonSet"},    "workload", "$1", "owner_name", "(.*)"  ))'
        labels: {
          workload_type: 'daemonset'
        }
      }
      {
        record: 'namespace_workload_pod:kube_pod_owner:relabel'
        expression: 'max by (cluster, namespace, workload, pod) (  label_replace(    kube_pod_owner{job="kube-state-metrics", owner_kind="StatefulSet"},    "workload", "$1", "owner_name", "(.*)"  ))'
        labels: {
          workload_type: 'statefulset'
        }
      }
      {
        record: 'namespace_workload_pod:kube_pod_owner:relabel'
        expression: 'max by (cluster, namespace, workload, pod) (  label_replace(    kube_pod_owner{job="kube-state-metrics", owner_kind="Job"},    "workload", "$1", "owner_name", "(.*)"  ))'
        labels: {
          workload_type: 'job'
        }
      }
      {
        record: ':node_memory_MemAvailable_bytes:sum'
        expression: 'sum(  node_memory_MemAvailable_bytes{job="node"} or  (    node_memory_Buffers_bytes{job="node"} +    node_memory_Cached_bytes{job="node"} +    node_memory_MemFree_bytes{job="node"} +    node_memory_Slab_bytes{job="node"}  )) by (cluster)'
      }
      {
        record: 'cluster:node_cpu:ratio_rate5m'
        expression: 'sum(rate(node_cpu_seconds_total{job="node",mode!="idle",mode!="iowait",mode!="steal"}[5m])) by (cluster) /count(sum(node_cpu_seconds_total{job="node"}) by (cluster, instance, cpu)) by (cluster)'
      }
    ]
  }
}

// UX Recording Rules - powers the Azure Portal Container Insights monitoring views
resource uxRecordingRulesRuleGroup 'Microsoft.AlertsManagement/prometheusRuleGroups@2023-03-01' = {
  name: uxRecordingRulesRuleGroupName
  location: azureMonitorWorkspaceLocation
  tags: {
    managedBy: 'cache-server-infra'
  }
  properties: {
    description: 'UX Recording Rules for Linux'
    scopes: [
      azureMonitorWorkspaceId
      aks.id
    ]
    enabled: true
    clusterName: clusterName
    interval: 'PT1M'
    rules: [
      {
        record: 'ux:pod_cpu_usage:sum_irate'
        expression: '''(sum by (namespace, pod, cluster, microsoft_resourceid) (
  irate(container_cpu_usage_seconds_total{container != "", pod != "", job = "cadvisor"}[5m])
)) * on (pod, namespace, cluster, microsoft_resourceid) group_left (node, created_by_name, created_by_kind)
(max by (node, created_by_name, created_by_kind, pod, namespace, cluster, microsoft_resourceid) (kube_pod_info{pod != "", job = "kube-state-metrics"}))'''
      }
      {
        record: 'ux:controller_cpu_usage:sum_irate'
        expression: '''sum by (namespace, node, cluster, created_by_name, created_by_kind, microsoft_resourceid) (
ux:pod_cpu_usage:sum_irate
)'''
      }
      {
        record: 'ux:pod_workingset_memory:sum'
        expression: '''(
  sum by (namespace, pod, cluster, microsoft_resourceid) (
    container_memory_working_set_bytes{container != "", pod != "", job = "cadvisor"}
  )
) * on (pod, namespace, cluster, microsoft_resourceid) group_left (node, created_by_name, created_by_kind)
(max by (node, created_by_name, created_by_kind, pod, namespace, cluster, microsoft_resourceid) (kube_pod_info{pod != "", job = "kube-state-metrics"}))'''
      }
      {
        record: 'ux:controller_workingset_memory:sum'
        expression: '''sum by (namespace, node, cluster, created_by_name, created_by_kind, microsoft_resourceid) (
ux:pod_workingset_memory:sum
)'''
      }
      {
        record: 'ux:pod_rss_memory:sum'
        expression: '''(
  sum by (namespace, pod, cluster, microsoft_resourceid) (
    container_memory_rss{container != "", pod != "", job = "cadvisor"}
  )
) * on (pod, namespace, cluster, microsoft_resourceid) group_left (node, created_by_name, created_by_kind)
(max by (node, created_by_name, created_by_kind, pod, namespace, cluster, microsoft_resourceid) (kube_pod_info{pod != "", job = "kube-state-metrics"}))'''
      }
      {
        record: 'ux:controller_rss_memory:sum'
        expression: '''sum by (namespace, node, cluster, created_by_name, created_by_kind, microsoft_resourceid) (
ux:pod_rss_memory:sum
)'''
      }
      {
        record: 'ux:pod_container_count:sum'
        expression: '''sum by (node, created_by_name, created_by_kind, namespace, cluster, pod, microsoft_resourceid) (
((
sum by (container, pod, namespace, cluster, microsoft_resourceid) (kube_pod_container_info{container != "", pod != "", container_id != "", job = "kube-state-metrics"})
or sum by (container, pod, namespace, cluster, microsoft_resourceid) (kube_pod_init_container_info{container != "", pod != "", container_id != "", job = "kube-state-metrics"})
)
* on (pod, namespace, cluster, microsoft_resourceid) group_left (node, created_by_name, created_by_kind)
(
max by (node, created_by_name, created_by_kind, pod, namespace, cluster, microsoft_resourceid) (
  kube_pod_info{pod != "", job = "kube-state-metrics"}
)
)
)
)'''
      }
      {
        record: 'ux:controller_container_count:sum'
        expression: '''sum by (node, created_by_name, created_by_kind, namespace, cluster, microsoft_resourceid) (
ux:pod_container_count:sum
)'''
      }
      {
        record: 'ux:pod_container_restarts:max'
        expression: '''max by (node, created_by_name, created_by_kind, namespace, cluster, pod, microsoft_resourceid) (
((
max by (container, pod, namespace, cluster, microsoft_resourceid) (kube_pod_container_status_restarts_total{container != "", pod != "", job = "kube-state-metrics"})
or sum by (container, pod, namespace, cluster, microsoft_resourceid) (kube_pod_init_status_restarts_total{container != "", pod != "", job = "kube-state-metrics"})
)
* on (pod, namespace, cluster, microsoft_resourceid) group_left (node, created_by_name, created_by_kind)
(
max by (node, created_by_name, created_by_kind, pod, namespace, cluster, microsoft_resourceid) (
  kube_pod_info{pod != "", job = "kube-state-metrics"}
)
)
)
)'''
      }
      {
        record: 'ux:controller_container_restarts:max'
        expression: '''max by (node, created_by_name, created_by_kind, namespace, cluster, microsoft_resourceid) (
ux:pod_container_restarts:max
)'''
      }
      {
        record: 'ux:pod_resource_limit:sum'
        expression: '''(sum by (cluster, pod, namespace, resource, microsoft_resourceid) (
(
  max by (cluster, microsoft_resourceid, pod, container, namespace, resource)
   (kube_pod_container_resource_limits{container != "", pod != "", job = "kube-state-metrics"})
)
)unless (count by (pod, namespace, cluster, resource, microsoft_resourceid)
  (kube_pod_container_resource_limits{container != "", pod != "", job = "kube-state-metrics"})
!= on (pod, namespace, cluster, microsoft_resourceid) group_left()
 sum by (pod, namespace, cluster, microsoft_resourceid)
 (kube_pod_container_info{container != "", pod != "", job = "kube-state-metrics"})
)
)* on (namespace, pod, cluster, microsoft_resourceid) group_left (node, created_by_kind, created_by_name)
(
  kube_pod_info{pod != "", job = "kube-state-metrics"}
)'''
      }
      {
        record: 'ux:controller_resource_limit:sum'
        expression: '''sum by (cluster, namespace, created_by_name, created_by_kind, node, resource, microsoft_resourceid) (
ux:pod_resource_limit:sum
)'''
      }
      {
        record: 'ux:controller_pod_phase_count:sum'
        expression: '''sum by (cluster, phase, node, created_by_kind, created_by_name, namespace, microsoft_resourceid) (
((kube_pod_status_phase{job="kube-state-metrics",pod!=""})
 or (label_replace((count(kube_pod_deletion_timestamp{job="kube-state-metrics",pod!=""}) by (namespace, pod, cluster, microsoft_resourceid) * count(kube_pod_status_reason{reason="NodeLost", job="kube-state-metrics"} == 0) by (namespace, pod, cluster, microsoft_resourceid)), "phase", "terminating", "", ""))) * on (pod, namespace, cluster, microsoft_resourceid) group_left (node, created_by_name, created_by_kind)
(
max by (node, created_by_name, created_by_kind, pod, namespace, cluster, microsoft_resourceid)
(kube_pod_info{job="kube-state-metrics",pod!=""})
)
)'''
      }
      {
        record: 'ux:cluster_pod_phase_count:sum'
        expression: '''sum by (cluster, phase, node, namespace, microsoft_resourceid) (
ux:controller_pod_phase_count:sum
)'''
      }
      {
        record: 'ux:node_cpu_usage:sum_irate'
        expression: '''sum by (instance, cluster, microsoft_resourceid) (
(1 - irate(node_cpu_seconds_total{job="node", mode="idle"}[5m]))
)'''
      }
      {
        record: 'ux:node_memory_usage:sum'
        expression: '''sum by (instance, cluster, microsoft_resourceid) ((
node_memory_MemTotal_bytes{job = "node"}
- node_memory_MemFree_bytes{job = "node"}
- node_memory_cached_bytes{job = "node"}
- node_memory_buffers_bytes{job = "node"}
))'''
      }
      {
        record: 'ux:node_network_receive_drop_total:sum_irate'
        expression: '''sum by (instance, cluster, microsoft_resourceid) (irate(node_network_receive_drop_total{job="node", device!="lo"}[5m]))'''
      }
      {
        record: 'ux:node_network_transmit_drop_total:sum_irate'
        expression: '''sum by (instance, cluster, microsoft_resourceid) (irate(node_network_transmit_drop_total{job="node", device!="lo"}[5m]))'''
      }
    ]
  }
}
