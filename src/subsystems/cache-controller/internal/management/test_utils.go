// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

package management

import (
	"fmt"

	v1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/resource"
)

// helper to create mock GPU node list
func CreateMockGpuNodeList(nodeCount int) []Node {
	return CreateMockNodeList(nodeCount, "gpu", "")
}

// helper to create mock node list
func CreateMockNodeList(nodeCount int, processingUnit string, agentPool string) []Node {
	nodes := []Node{}

	for i := 0; i < nodeCount; i++ {
		nodeLabels := map[string]string{}
		if processingUnit != "" {
			nodeLabels["dacs.azure.com/processing-unit"] = processingUnit
		}
		if agentPool != "" {
			nodeLabels["agentpool"] = agentPool
		}

		allocatable := v1.ResourceList{
			v1.ResourceCPU:             resource.MustParse("4"),
			v1.ResourceMemory:          resource.MustParse("16Gi"),
			"nvidia.com/gpu":           resource.MustParse("1"),
			"rdma/hca_rdma_infiniband": resource.MustParse("1"),
		}

		nodes = append(nodes, Node{
			NodeName:    fmt.Sprintf("node-%d", i),
			Schedulable: true,
			NodeLabels:  nodeLabels,
			Allocatable: allocatable,
		})
	}
	return nodes
}

func CreateMockPod(namespace string, podIdx int, nodeName string, state PodStatus, runOnGpu bool) Pod {
	nodeSelectorValue := "cpu"
	if runOnGpu {
		nodeSelectorValue = "gpu"
	}

	requests := v1.ResourceList{
		v1.ResourceCPU: resource.MustParse("100m"),
	}

	return Pod{
		Namespace:           namespace,
		PodName:             fmt.Sprintf("cacheserver-%d", podIdx),
		NodeName:            nodeName,
		State:               state,
		SchedulerBypassed:   false,
		NodeSelectorKey:     "dacs.azure.com/processing-unit",
		NodeSelectorValue:   nodeSelectorValue,
		UseEphemeralStorage: false,
		Requests:            requests,
	}
}
