/*
Copyright (c) Microsoft Corporation.
Licensed under the Apache License, Version 2.0.
*/

package v1

import (
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

// ScalingType defines the scaling configuration for cache servers.
// Specify either dynamic or static (mutually exclusive).
type ScalingType struct {
	// Dynamic configuration for scaling based on node availability
	// +optional
	Dynamic *DynamicScaleConfig `json:"dynamic,omitempty"`

	// Static configuration for a fixed number of cache servers
	// +optional
	Static *StaticScaleConfig `json:"static,omitempty"`
}

// DynamicScaleConfig defines automatic scaling parameters based on cluster node availability.
// The controller will maintain cache servers as a percentage of available nodes.
type DynamicScaleConfig struct {
	// Percentage of available nodes to use for cache servers
	// +required
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:validation:Maximum=100
	PercentageOfNodesToUse int32 `json:"percentageOfNodesToUse"`

	// Minimum number of cache servers to run. If the percentage-based count
	// falls below this threshold, the replica count is floored to 0 instead
	// of being clamped up - the cache will not run at all until enough nodes
	// are available to meet the minimum.
	// +optional
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:validation:Maximum=499
	MinServers *int32 `json:"minServers,omitempty"`

	// Maximum number of cache servers to run. The replica count will never
	// exceed this value regardless of the percentage calculation.
	// +optional
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:validation:Maximum=499
	MaxServers *int32 `json:"maxServers,omitempty"`
}

// StaticScaleConfig defines a static number of cache servers.
// The controller will maintain exactly this many replicas.
type StaticScaleConfig struct {
	// Number of cache server replicas to maintain
	// +required
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:validation:Maximum=499
	NumServers int32 `json:"numServers"`
}

// EvictionConfig defines cache eviction related settings.
type EvictionConfig struct {
	// Enable accessTime based eviction
	// +optional
	// +kubebuilder:default=true
	EnableAccessTimeEviction *bool `json:"enableAccessTimeEviction,omitempty"`

	// Keep a portion of cache space always free even if all data is within access time window
	// +optional
	// +kubebuilder:default=true
	EvictionMaintainFreeSpace *bool `json:"evictionMaintainFreeSpace,omitempty"`

	// Last Access threshold for cache eviction, example "30d12h41m29s"
	// +optional
	// +kubebuilder:default="30d"
	LastAccessThreshold *string `json:"lastAccessThreshold,omitempty"`

	// Size threshold for triggering cache eviction in percentage (0-100).
	// Eviction starts when cache utilization exceeds this value.
	// +optional
	// +kubebuilder:default="90"
	SizeThreshold *string `json:"sizeThreshold,omitempty"`

	// Frequency of running eviction pass, example "1h30m"
	// +optional
	// +kubebuilder:default="1h"
	PassFrequency *string `json:"passFrequency,omitempty"`

	// Target cache utilization percentage after eviction (0-100).
	// Eviction runs until cache utilization drops to this level.
	// Must be less than SizeThreshold to avoid continuous eviction cycles.
	// +optional
	// +kubebuilder:default="80"
	SizeTarget *string `json:"sizeTarget,omitempty"`
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
	// Scaling configuration for cache servers
	// Specify either dynamic or static (mutually exclusive)
	// +required
	Scaling ScalingType `json:"scaling"`

	// Node selector label key for cache server pods
	// +optional
	// +kubebuilder:default="dacs.azure.com/processing-unit"
	NodeSelectorKey *string `json:"nodeSelectorKey,omitempty"`

	// Node selector label value for cache server pods
	// +optional
	// +kubebuilder:default="gpu"
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
	// +kubebuilder:default="rdma/hca_rdma_infiniband"
	InfinibandResourceLimitName *string `json:"infinibandResourceLimitName,omitempty"`

	// Infiniband resource limit count (set to 0 to disable infiniband)
	// +optional
	// +kubebuilder:default=0
	// +kubebuilder:validation:Minimum=0
	InfinibandResourceLimitCount *int32 `json:"infinibandResourceLimitCount,omitempty"`

	// Port for cache server pods
	// +optional
	// +kubebuilder:default=9065
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:validation:Maximum=65535
	Port *int32 `json:"port,omitempty"`

	// Metrics port for cache server pods
	// +optional
	// +kubebuilder:default=9096
	// +kubebuilder:validation:Minimum=1
	// +kubebuilder:validation:Maximum=65535
	MetricsPort *int32 `json:"metricsPort,omitempty"`

	// Eviction settings
	// +optional
	Eviction *EvictionConfig `json:"eviction,omitempty"`

	// Storage configuration for cache data. Default is hostPath storage with path /var/lib/ssd/cacheserver
	// +optional
	Storage *StorageConfig `json:"storage,omitempty"`
}

// ScalingStatus reports the current state of the scaling normalizer.
// Present only when dynamic scaling is active and the controller has
// successfully computed a replica target. Nil for static scaling or
// when the scaling calculation could not be performed (error paths).
//
// A non-nil StabilizationEnds indicates a stabilization window is
// holding back a scaling change. When StabilizationEnds is nil the
// system has settled at TargetServers.
type ScalingStatus struct {
	// The server count the scaling algorithm computed. When a
	// stabilization window is active this is the count the system
	// would apply once the window expires. Nil on error paths where
	// the target could not be determined.
	// +optional
	TargetServers *int32 `json:"targetServers,omitempty"`

	// When the current stabilization window expires. Nil when
	// scaling has settled.
	// +optional
	StabilizationEnds *metav1.Time `json:"stabilizationEnds,omitempty"`

	// Human-readable summary of the current scaling state.
	// +optional
	Message string `json:"message,omitempty"`
}

// CacheStatus defines the observed state of Cache.
type CacheStatus struct {
	// conditions represent the current state of the Cache resource.
	// Each condition has a unique type and reflects the status of a specific aspect of the resource.
	//
	// All conditions use positive polarity (True = good/healthy). This aligns with the
	// kstatus convention so that tooling such as `kubectl wait --for=condition=Ready` works
	// out of the box. See https://github.com/kubernetes-sigs/cli-utils/blob/master/pkg/kstatus/README.md
	//
	// Condition types:
	// - "Ready": overall readiness derived from all other conditions
	// - "ReplicasReady": whether the desired number of StatefulSet replicas are running and ready
	// - "PodsScheduled": whether all cache server pods have been successfully scheduled.
	//   False when pods are stuck pending or scheduling errors occurred.
	//
	// The status of each condition is one of True, False, or Unknown.
	// +listType=map
	// +listMapKey=type
	// +optional
	Conditions []metav1.Condition `json:"conditions,omitempty"`

	// Scaling reports the state of the scaling normalizer's stabilization
	// window. Nil when static scaling is configured or when the scaling
	// calculation could not be performed.
	// +optional
	Scaling *ScalingStatus `json:"scaling,omitempty"`

	// Desired number of cache server pods
	// +optional
	DesiredServers *int32 `json:"desiredServers,omitempty"`

	// Current number of cache server pods
	// +optional
	CurrentServers *int32 `json:"currentServers,omitempty"`

	// The discovery URL for the cache.
	// Pass this to cache client to connect to cache.
	// +optional
	DiscoveryURL string `json:"discoveryURL,omitempty"`
}

// +kubebuilder:object:root=true
// +kubebuilder:subresource:status
// +kubebuilder:printcolumn:name="Ready",type=string,JSONPath=`.status.conditions[?(@.type=="Ready")].status`
// +kubebuilder:printcolumn:name="Status",type=string,JSONPath=`.status.conditions[?(@.type=="Ready")].message`
// +kubebuilder:printcolumn:name="Current",type=integer,JSONPath=`.status.currentServers`
// +kubebuilder:printcolumn:name="Desired",type=integer,JSONPath=`.status.desiredServers`
// +kubebuilder:printcolumn:name="Age",type=date,JSONPath=`.metadata.creationTimestamp`
// +kubebuilder:printcolumn:name="Discovery URL",type=string,JSONPath=`.status.discoveryURL`
// +kubebuilder:printcolumn:name="Size(GB)",type=integer,JSONPath=`.spec.serverSizeInGB`,priority=1
// +kubebuilder:printcolumn:name="Memory(MB)",type=integer,JSONPath=`.spec.memoryLimitMB`,priority=1
// +kubebuilder:printcolumn:name="Threads",type=integer,JSONPath=`.spec.serverThreads`,priority=1
// +kubebuilder:printcolumn:name="IB Resources",type=integer,JSONPath=`.spec.infinibandResourceLimitCount`,priority=1
// +kubebuilder:printcolumn:name="Selector Key",type=string,JSONPath=`.spec.nodeSelectorKey`,priority=1
// +kubebuilder:printcolumn:name="Selector Value",type=string,JSONPath=`.spec.nodeSelectorValue`,priority=1
//
// Name validation: the controller derives sub-resource names by appending
// suffixes to metadata.name (longest: "-discovery", 10 chars). Service names
// must be valid RFC 1123 DNS labels (max 63 chars), so the Cache name is
// capped at 53 characters and must match the DNS label pattern. When using
// metadata.generateName, the apiserver appends a 5-char random suffix to form
// the final name, so the prefix is capped at 48 characters.
//
// Names ending in "-discovery" are also rejected: the controller creates a
// Service named "{cacheName}-discovery", so a Cache literally named
// "{x}-discovery" would have a main Service that collides with the discovery
// Service derived from a Cache named "{x}".
// +kubebuilder:validation:XValidation:rule="has(self.metadata.name) || has(self.metadata.generateName)",message="must specify metadata.name or metadata.generateName"
// +kubebuilder:validation:XValidation:rule="!has(self.metadata.name) || size(self.metadata.name) <= 53",message="metadata.name must be at most 53 characters; the controller appends suffixes up to 10 characters to create Service names, which are limited to 63"
// +kubebuilder:validation:XValidation:rule="!has(self.metadata.name) || self.metadata.name.matches('^[a-z0-9]([a-z0-9-]*[a-z0-9])?$')",message="metadata.name must be a valid RFC 1123 DNS label: lowercase alphanumeric and hyphens, must start and end with alphanumeric"
// +kubebuilder:validation:XValidation:rule="!has(self.metadata.name) || !self.metadata.name.endsWith('-discovery')",message="metadata.name must not end with '-discovery'; that suffix is reserved for the discovery Service derived from another Cache and would cause a Service name collision"
// +kubebuilder:validation:XValidation:rule="!has(self.metadata.generateName) || size(self.metadata.generateName) <= 48",message="metadata.generateName must be at most 48 characters; the apiserver appends a 5-char suffix and the controller appends suffixes up to 10 characters to create Service names, which are limited to 63"
// +kubebuilder:validation:XValidation:rule="!has(self.metadata.generateName) || self.metadata.generateName.matches('^[a-z0-9]([a-z0-9-]*)?$')",message="metadata.generateName must be a valid RFC 1123 DNS label prefix: lowercase alphanumeric and hyphens, must start with alphanumeric"

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
