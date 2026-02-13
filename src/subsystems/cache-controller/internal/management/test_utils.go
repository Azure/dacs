package management

import (
	"fmt"
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
		nodes = append(nodes, Node{
			NodeName:    fmt.Sprintf("node-%d", i),
			Schedulable: true,
			NodeLabels:  nodeLabels,
		})
	}
	return nodes
}

func CreateMockPod(namespace string, podIdx int, nodeName string, state PodStatus, runOnGpu bool) Pod {
	nodeSelectorValue := "cpu"
	if runOnGpu {
		nodeSelectorValue = "gpu"
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
	}
}
