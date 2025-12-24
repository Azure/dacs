/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package v1

import (
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

// EDIT THIS FILE!  THIS IS SCAFFOLDING FOR YOU TO OWN!
// NOTE: json tags are required.  Any new fields you add must have json tags for the fields to be serialized.

// ScalingType defines the scaling configuration for cache servers.
// Specify either auto or fixed (mutually exclusive).
type ScalingType struct {
	// Auto configuration for dynamic scaling based on node availability
	// +optional
	Auto *AutoScaleConfig `json:"auto,omitempty"`

	// Fixed configuration for a static number of cache servers
	// +optional
	Fixed *FixedScaleConfig `json:"fixed,omitempty"`
}

// AutoScaleConfig defines automatic scaling parameters based on cluster node availability.
// The controller will maintain cache servers as a percentage of available nodes,
// constrained by minimum and maximum bounds.
type AutoScaleConfig struct {
	// Percentage of available nodes to use for cache servers
	// +required
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:validation:Maximum=100
	PercentageOfNodesToUse int32 `json:"percentageOfNodesToUse"`

	// Minimum number of cache servers to maintain
	// +optional
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:validation:Maximum=99
	MinServers *int32 `json:"minServers,omitempty"`

	// Maximum number of cache servers to maintain
	// +optional
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:validation:Maximum=99
	MaxServers *int32 `json:"maxServers,omitempty"`
}

// FixedScaleConfig defines a static number of cache servers.
// The controller will maintain exactly this many replicas.
type FixedScaleConfig struct {
	// Number of cache server replicas to maintain
	// +required
	// +kubebuilder:validation:Minimum=1
	NumServers int32 `json:"numServers"`
}

// EvictionConfig defines cache eviction related settings.
type EvictionConfig struct {
	// Enable accessTime based eviction
	// +optional
	// +kubebuilder:default:=true
	EnableAccessTimeEviction *bool `json:"enableAccessTimeEviction,omitempty"`

	// Keep a portion of cache space always free even if all data is within access time window
	// +optional
	// +kubebuilder:default:=true
	EvictionMaintainFreeSpace *bool `json:"evictionMaintainFreeSpace,omitempty"`
}

// StorageConfig defines storage configuration for cache data.
// Specify one of: ephemeral or hostPath(mutually exclusive)
type StorageConfig struct {
	// Use ephemeral storage for cache data storage
	// +optional
	Ephemeral *bool `json:"ephemeral,omitempty"`

	// Use hostPath volume for cache data storage
	// +optional
	HostPath *HostPathConfig `json:"hostPath,omitempty"`
}

type HostPathConfig struct {
	// Node cache folder path (used if useHostPath is true)
	// +required
	NodeCacheFolderPath string `json:"nodeCacheFolderPath"`
}

// CacheSpec defines the desired state of Cache
type CacheSpec struct {
	// INSERT ADDITIONAL SPEC FIELDS - desired state of cluster
	// Important: Run "make" to regenerate code after modifying this file
	// The following markers will use OpenAPI v3 schema to validate the value
	// More info: https://book.kubebuilder.io/reference/markers/crd-validation.html

	// Scaling configuration for cache servers
	// Specify either autoScale or fixedScale (mutually exclusive)
	// +required
	Scaling ScalingType `json:"scaling"`

	// Node selector label key for cache server pods
	// +optional
	// +kubebuilder:default:="dacs.azure.com/processing-unit"
	NodeSelectorKey *string `json:"nodeSelectorKey,omitempty"`

	// Node selector label value for cache server pods
	// +optional
	// +kubebuilder:default:="gpu"
	NodeSelectorValue *string `json:"nodeSelectorValue,omitempty"`

	// Max allowed cache size in GB per cache server
	// +required
	// +kubebuilder:validation:Minimum=1
	ServerSizeInGB int32 `json:"serverSizeInGB"`

	// Number of threads per cache server
	// +required
	// +kubebuilder:validation:Minimum=4
	ServerThreads int32 `json:"serverThreads"`

	// CPU limit per cache server pod (recommended when running cache on non-dedicated nodes)
	// +optional
	// +kubebuilder:validation:Minimum=1
	CpuLimit *int32 `json:"cpuLimit,omitempty"`

	// Memory limit in MB per cache server pod (recommended when running cache on non-dedicated nodes)
	// +optional
	// +kubebuilder:validation:Minimum=1024
	MemoryLimitMB *int32 `json:"memoryLimitMB,omitempty"`

	// Egress Bandwidth limit in Gbps (recommended when running cache on non-dedicated nodes)
	// +optional
	// +kubebuilder:validation:Minimum=1
	EgressBandwidthLimitGbps *int32 `json:"egressBandwidthLimitGbps,omitempty"`

	// Infiniband resource limit name
	// +optional
	// +kubebuilder:default:="rdma/hca_rdma_infiniband"
	InfinibandResourceLimitName *string `json:"infinibandResourceLimitName,omitempty"`

	// Infiniband resource limit count (set to 0 to disable infiniband)
	// +optional
	// +kubebuilder:default:=0
	InfinibandResourceLimitCount *int32 `json:"infinibandResourceLimitCount,omitempty"`

	// Port for cache server pods
	// +optional
	// +kubebuilder:default:=9065
	Port *int32 `json:"port,omitempty"`

	// Metrics port for cache server pods
	// +optional
	// +kubebuilder:default:=9096
	MetricsPort *int32 `json:"metricsPort,omitempty"`

	// Eviction settings
	// +optional
	Eviction *EvictionConfig `json:"eviction,omitempty"`

	// Storage configuration for cache data. Default is hostPath storage with path /var/lib/ssd/cacheserver
	// +optional
	Storage *StorageConfig `json:"storage,omitempty"`
}

// CacheStatus defines the observed state of Cache.
type CacheStatus struct {
	// INSERT ADDITIONAL STATUS FIELD - define observed state of cluster
	// Important: Run "make" to regenerate code after modifying this file

	// For Kubernetes API conventions, see:
	// https://github.com/kubernetes/community/blob/master/contributors/devel/sig-architecture/api-conventions.md#typical-status-properties

	// conditions represent the current state of the Cache resource.
	// Each condition has a unique type and reflects the status of a specific aspect of the resource.
	//
	// Standard condition types include:
	// - "Available": the resource is fully functional
	// - "Progressing": the resource is being created or updated
	// - "Degraded": the resource failed to reach or maintain its desired state
	//
	// The status of each condition is one of True, False, or Unknown.
	// +listType=map
	// +listMapKey=type
	// +optional
	Conditions []metav1.Condition `json:"conditions,omitempty"`

	// Desired number of cache server pods
	// +optional
	DesiredServers *int32 `json:"desiredServers,omitempty"`

	// Current number of cache server pods
	// +optional
	CurrentServers *int32 `json:"currentServers,omitempty"`
}

// +kubebuilder:object:root=true
// +kubebuilder:subresource:status

// Cache is the Schema for the caches API
type Cache struct {
	metav1.TypeMeta `json:",inline"`

	// metadata is a standard object metadata
	// +optional
	metav1.ObjectMeta `json:"metadata,omitzero"`

	// spec defines the desired state of Cache
	// +required
	Spec CacheSpec `json:"spec"`

	// status defines the observed state of Cache
	// +optional
	Status CacheStatus `json:"status,omitzero"`
}

// +kubebuilder:object:root=true

// CacheList contains a list of Cache
type CacheList struct {
	metav1.TypeMeta `json:",inline"`
	metav1.ListMeta `json:"metadata,omitzero"`
	Items           []Cache `json:"items"`
}

func init() {
	SchemeBuilder.Register(&Cache{}, &CacheList{})
}
