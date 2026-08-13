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

package management

import (
	v1 "k8s.io/api/core/v1"
)

// Cacheserver pod status.
// Values can be either from v1.PodPhase or PodStatusTerminating or PodStatusNotReady.
type PodStatus string

const (
	// Pod is in pending state.
	PodStatusPending PodStatus = PodStatus(v1.PodPending)

	// Pod is in running state.
	PodStatusRunning PodStatus = PodStatus(v1.PodRunning)

	// Pod is in succeeded state.
	PodStatusSucceeded PodStatus = PodStatus(v1.PodSucceeded)

	// Pod is in failed state.
	PodStatusFailed PodStatus = PodStatus(v1.PodFailed)

	// Pod is in unknown state.
	PodStatusUnknown PodStatus = PodStatus(v1.PodUnknown)

	// Pod is stuck in terminating state.
	PodStatusTerminating PodStatus = "Terminating"

	// One or more containers in the pod is not ready.
	PodStatusNotReady PodStatus = "NotReady"
)

// Cacheserver pod definition
type Pod struct {
	// Name of the pod
	PodName string `json:"podName"`
	// Namespace of the pod
	Namespace string `json:"namespace"`
	// Name of the node if pod is scheduled
	NodeName string `json:"nodeName"`
	// State of the pod
	State PodStatus `json:"state"`
	// Node selector key allowed for scheduling
	NodeSelectorKey string `json:"nodeSelectorKey"`
	// Node selector value allowed for scheduling
	NodeSelectorValue string `json:"nodeSelectorValue"`
	// Is schedulerBypassed
	SchedulerBypassed bool `json:"schedulerBypassed"`
	// Does it use ephemeral storage
	UseEphemeralStorage bool `json:"allowEphemeralStorage"`
	// Resource requested by the pod
	Requests v1.ResourceList `json:"requests"`
}

// Cacheserver node definition
type Node struct {
	// Name of the node
	NodeName string `json:"nodeName"`
	// Is node schedulable (i.e not cordoned)
	Schedulable bool `json:"schedulable"`
	// List of node labels (key value pair)
	NodeLabels map[string]string `json:"nodeLabels"`
	// Allocatable resources of the node
	Allocatable v1.ResourceList `json:"allocatable"`
}
