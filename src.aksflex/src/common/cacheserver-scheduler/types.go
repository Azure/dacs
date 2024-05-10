package main

import (
	v1 "k8s.io/api/core/v1"
)

// Cacheserver pod definition
type Pod struct {
	// Name of the pod
	PodName string `json:"podName"`
	// Name of the node if pod is scheduled
	NodeName string `json:"nodeName"`
	// State of the pod
	State v1.PodPhase `json:"state"`
	// Is schedulerBypassed
	SchedulerBypassed bool `json:"schedulerBypassed"`
	// List of allowed processing units (node label)
	AllowedProcessingUnits []string `json:"allowedProcessingUnits"`
	// List of allowed agentpools
	AllowedAgentpools []string `json:"allowedAgentpools"`
	// Does it use ephemeral storage
	UseEphemeralStorage bool `json:"allowEphemeralStorage"`
}

// Cacheserver node definition
type Node struct {
	// Name of the node
	NodeName string `json:"nodeName"`
	// Is node schedulable (i.e not cordoned)
	Schedulable bool `json:"schedulable"`
	// Processing unit label value
	ProcessingUnit string `json:"processingUnit"`
	// Agentpool label value
	Agentpool string `json:"agentpool"`
}
