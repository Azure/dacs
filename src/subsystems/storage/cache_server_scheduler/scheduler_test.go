// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

package main

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"
	"testing"
	"time"

	"gotest.tools/v3/assert"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	v1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/api/resource"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/runtime/schema"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/kubernetes/fake"
	k8stesting "k8s.io/client-go/testing"
)

func init() {
	initLogger("debug")
}

// helper to create mock node list
func createMockNodeList(nodeCount int) []Node {
	nodes := []Node{}

	for i := 0; i < nodeCount; i++ {
		allocatable := v1.ResourceList{
			v1.ResourceCPU:             resource.MustParse("4"),
			v1.ResourceMemory:          resource.MustParse("16Gi"),
			"nvidia.com/gpu":           resource.MustParse("1"),
			"rdma/hca_rdma_infiniband": resource.MustParse("1"),
		}

		nodes = append(nodes, Node{
			NodeName:       fmt.Sprintf("node-%d", i),
			Schedulable:    true,
			ProcessingUnit: "cpu",
			Agentpool:      "",
			Allocatable:    allocatable,
		})
	}
	return nodes
}

func createMockPod(podIdx int, nodeName string, state PodStatus, runOnCpu bool) Pod {
	var allowedProcessingUnits []string
	if runOnCpu {
		allowedProcessingUnits = []string{"cpu"}
	} else {
		allowedProcessingUnits = []string{"gpu"}
	}

	requests := v1.ResourceList{
		v1.ResourceCPU: resource.MustParse("100m"),
	}

	return Pod{
		PodName:                fmt.Sprintf("cacheserver-%d", podIdx),
		NodeName:               nodeName,
		State:                  state,
		SchedulerBypassed:      false,
		AllowedProcessingUnits: allowedProcessingUnits,
		AllowedAgentpools:      []string{},
		UseEphemeralStorage:    false,
		Requests:               requests,
	}
}

func getSchedulerInstructionForPod(schedulerInstructions []SchedulerInstruction, podName string) *SchedulerInstruction {
	for _, schedulerInstruction := range schedulerInstructions {
		if schedulerInstruction.PodName == podName {
			return &schedulerInstruction
		}
	}

	return nil
}

func checkSchedulerInstructionForPod(
	t *testing.T,
	schedulerInstructions []SchedulerInstruction,
	podName string,
	expectedType SchedulerInstructionType,
	allowedDestinationNodes nodeList,
	expectedReason string) {

	schedulerInstruction := getSchedulerInstructionForPod(schedulerInstructions, podName)
	assert.Assert(t, schedulerInstruction != nil)
	assert.Equal(t, schedulerInstruction.Type, expectedType)
	if len(allowedDestinationNodes) == 0 {
		assert.Equal(t, schedulerInstruction.DestinationNode, "")
	} else {
		assert.Assert(t, allowedDestinationNodes[schedulerInstruction.DestinationNode])
	}

	assert.Equal(t, schedulerInstruction.Reason, expectedReason)
}

func assertPodScheduledToNode(
	t *testing.T,
	schedulerInstructions []SchedulerInstruction,
	podName string,
	expectedNodeName string,
	expectedReason string) {

	expectedNodeList := map[string]bool{expectedNodeName: true}
	checkSchedulerInstructionForPod(t, schedulerInstructions, podName, BindInstruction, expectedNodeList, expectedReason)
}

func assertPodScheduledToOneOfNodes(
	t *testing.T,
	schedulerInstructions []SchedulerInstruction,
	podName string,
	expectedNodeNames nodeList,
	expectedReason string) {

	checkSchedulerInstructionForPod(t, schedulerInstructions, podName, BindInstruction, expectedNodeNames, expectedReason)
}

func assertPodDeleted(
	t *testing.T,
	schedulerInstructions []SchedulerInstruction,
	podName string,
	expectedReason string) {

	checkSchedulerInstructionForPod(t, schedulerInstructions, podName, DeleteInstruction, nodeList{}, expectedReason)
}

// helper to mark pod as pending
func (pod *Pod) markPodAsPending() {
	pod.State = PodStatusPending
	pod.NodeName = ""
}

// helper to mark pod as running
func (pod *Pod) markPodAsRunning(nodeName string) {
	pod.State = PodStatusRunning
	pod.NodeName = nodeName
}

func resetSchedulerState() {
	schedulerState = schedulerInternalState{
		NodeMap: map[string]schedulerNodeInfo{},
		PodMap:  map[string]schedulerPodInfo{},
	}
}

// mockLazyInit sets the clientset to the provided fake clientset.
func mockLazyInit(fakeClientset kubernetes.Interface) {
	clientset = fakeClientset
}

// Helper function to create a mock Kubernetes StatefulSet
func createMockStatefulSet(name string, namespace string, replicas int32) *appsv1.StatefulSet {
	return &appsv1.StatefulSet{
		ObjectMeta: metav1.ObjectMeta{
			Name:      name,
			Namespace: namespace,
			// No labels on the StatefulSet itself - verifies we fetch by name, not label
		},
		Spec: appsv1.StatefulSetSpec{
			Replicas: &replicas,
			Selector: &metav1.LabelSelector{
				MatchLabels: map[string]string{
					"app": "cacheserver",
				},
			},
			Template: corev1.PodTemplateSpec{
				ObjectMeta: metav1.ObjectMeta{
					Labels: map[string]string{
						"app": "cacheserver",
					},
				},
			},
		},
	}
}

func TestPodSticksToNode(t *testing.T) {
	resetSchedulerState()

	// create 12 nodes
	nodes := createMockNodeList(12)

	pods := []Pod{}
	pods = append(pods, createMockPod(0, "node-8", PodStatusRunning, true))
	pods = append(pods, createMockPod(1, "node-0", PodStatusRunning, true))
	pods = append(pods, createMockPod(2, "node-10", PodStatusRunning, true))

	// Initial, all pods running - should not get any schedule instructions
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark pod-2 as pending, and reprocess. Pod-2 should get scheduled to node-10
	pods[2].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-2", "node-10", "Rescheduling pod on the node it was previously running on")

	// Mark pod-2 as running again, and reprocess. We should not get any schedule instructions
	pods[2].markPodAsRunning("node-10")
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark pod-1 as pending, and reprocess. Pod-1 should get scheduled to node-0
	pods[1].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-1", "node-0", "Rescheduling pod on the node it was previously running on")

	// Mark pod-1 as running again, and reprocess. We should not get any schedule instructions
	pods[1].markPodAsRunning("node-0")
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark pod-0 as pending, and reprocess. Pod-0 should get scheduled to node-8
	pods[0].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-0", "node-8", "Rescheduling pod on the node it was previously running on")

	// Mark pod-0 as running again, and reprocess. We should not get any schedule instructions
	pods[0].markPodAsRunning("node-8")
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark all 3 pods as pending, and reprocess. We should get 3 schedule instructions with each pod getting scheduled to node it was previously running on
	pods[0].markPodAsPending()
	pods[1].markPodAsPending()
	pods[2].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 3)
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-0", "node-8", "Rescheduling pod on the node it was previously running on")
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-1", "node-0", "Rescheduling pod on the node it was previously running on")
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-2", "node-10", "Rescheduling pod on the node it was previously running on")

	// Mark all 3 pods as running again, and reprocess. We should not get any schedule instructions
	pods[0].markPodAsRunning("node-8")
	pods[1].markPodAsRunning("node-0")
	pods[2].markPodAsRunning("node-10")
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 0)
}

func TestPodAffinityHonoredProcessingUnit(t *testing.T) {
	resetSchedulerState()

	// create 6 nodes, 3 cpu (node-0, node-1, node-2) + 3 gpu (node-3, node-4, node-5)
	nodes := createMockNodeList(6)
	nodes[3].ProcessingUnit = "gpu"
	nodes[4].ProcessingUnit = "gpu"
	nodes[5].ProcessingUnit = "gpu"

	// create 3 pending pods, all with node affinity for cpu
	pods := []Pod{}
	pods = append(pods, createMockPod(0, "", PodStatusPending, true))
	pods = append(pods, createMockPod(1, "", PodStatusPending, true))
	pods = append(pods, createMockPod(2, "", PodStatusPending, true))

	// Initial, all pods pending - should get scheduled to cpu nodes
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 3)
	// Pod 0 should be scheduled to one of the 3 cpu nodes
	freeCpuNodes := nodeList{"node-0": true, "node-1": true, "node-2": true}
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cacheserver-0", freeCpuNodes, "Scheduling pod on healthy node")
	// Pod 1 should be scheduled to one of the 3 cpu nodes, excluding the node pod 0 was scheduled to
	freeCpuNodes.remove(getSchedulerInstructionForPod(schedulerInstructions, "cacheserver-0").DestinationNode)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cacheserver-1", freeCpuNodes, "Scheduling pod on healthy node")
	// Pod 2 should be scheduled to one of the 3 cpu nodes, excluding the nodes pod 0 and 1 were scheduled to
	freeCpuNodes.remove(getSchedulerInstructionForPod(schedulerInstructions, "cacheserver-1").DestinationNode)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cacheserver-2", freeCpuNodes, "Scheduling pod on healthy node")
}

func TestPodAffinityHonoredAgentpool(t *testing.T) {
	resetSchedulerState()
	// create 6 nodes, 3 with agentpool "system" and 3 with agentpool "user"
	nodes := createMockNodeList(6)
	nodes[0].Agentpool = "system"
	nodes[1].Agentpool = "system"
	nodes[2].Agentpool = "system"
	nodes[3].Agentpool = "user"
	nodes[4].Agentpool = "user"
	nodes[5].Agentpool = "user"

	// create 3 pending pods, all with node affinity for agentpool "user"
	pods := []Pod{}
	pods = append(pods, createMockPod(0, "", PodStatusPending, true))
	pods = append(pods, createMockPod(1, "", PodStatusPending, true))
	pods = append(pods, createMockPod(2, "", PodStatusPending, true))
	pods[0].AllowedAgentpools = []string{"user"}
	pods[1].AllowedAgentpools = []string{"user"}
	pods[2].AllowedAgentpools = []string{"user"}

	// Initial, all pods pending - should get scheduled to nodes with agentpool "user"
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 3)
	// Pod 0 should be scheduled to one of the 3 nodes with agentpool "user"
	freeUserNodes := nodeList{"node-3": true, "node-4": true, "node-5": true}
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cacheserver-0", freeUserNodes, "Scheduling pod on healthy node")
	// Pod 1 should be scheduled to one of the 3 nodes with agentpool "user", excluding the node pod 0 was scheduled to
	freeUserNodes.remove(getSchedulerInstructionForPod(schedulerInstructions, "cacheserver-0").DestinationNode)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cacheserver-1", freeUserNodes, "Scheduling pod on healthy node")
	// Pod 2 should be scheduled to one of the 3 nodes with agentpool "user", excluding the nodes pod 0 and 1 were scheduled to
	freeUserNodes.remove(getSchedulerInstructionForPod(schedulerInstructions, "cacheserver-1").DestinationNode)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cacheserver-2", freeUserNodes, "Scheduling pod on healthy node")
}

func TestContinousStuckPod(t *testing.T) {
	resetSchedulerState()

	// create 4 cpu nodes
	nodes := createMockNodeList(4)

	// create 3 pods, all running on different cpu nodes
	pods := []Pod{}
	pods = append(pods, createMockPod(0, "node-0", PodStatusRunning, true))
	pods = append(pods, createMockPod(1, "node-1", PodStatusRunning, true))
	pods = append(pods, createMockPod(2, "node-2", PodStatusRunning, true))

	// Initial, all pods running - should not get any schedule instructions
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 0)

	for podStuckCount := 1; podStuckCount <= 3; podStuckCount++ {
		// Mark pod-1 in Terminating state, and reprocess. We shouldn't get any schedule instructions yet
		pods[1].State = PodStatusTerminating
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
		assert.Equal(t, len(schedulerInstructions), 0)

		// Update the firstHitTime for pod-1 Unknown state to be 10 minutes ago, and reprocess. We should get delete instruction for stuck pod-1
		podState := schedulerState.PodMap["cacheserver-1"]
		podState.FirstHitTimestamp = time.Now().Add(-10 * time.Minute)
		schedulerState.PodMap["cacheserver-1"] = podState
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
		assert.Equal(t, len(schedulerInstructions), 1)
		assertPodDeleted(t, schedulerInstructions, "cacheserver-1", "Pod cacheserver-1 is stuck on node node-1 in Terminating state for more than 5m0s")

		// Mark pod-1 as pending, and reprocess. Pod-1 should get rescheduled on "node-1" first 2 times, and then on "node-3" the 3rd time
		pods[1].markPodAsPending()
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
		assert.Equal(t, len(schedulerInstructions), 1)
		expectedDestinationNode := "node-1"
		expectedReason := "Rescheduling pod on the node it was previously running on"
		if podStuckCount == 3 {
			expectedDestinationNode = "node-3"
			expectedReason = "Scheduling pod on healthy node"
		}
		assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-1", expectedDestinationNode, expectedReason)

		// Mark pod-1 as running, and reprocess. We should not get any schedule instructions
		pods[1].markPodAsRunning(expectedDestinationNode)
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
		assert.Equal(t, len(schedulerInstructions), 0)
	}

	// Mark node-3 as cordoned and pod-1 as pending, and reprocess. Pod-1 should get scheduled on "node-1"
	nodes[3].Schedulable = false
	pods[1].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-1", "node-1", "Scheduling pod on healthy node (warning: node has recent cacheserver errors)")
}

func TestTransientStuckPod(t *testing.T) {
	resetSchedulerState()

	// create 6 cpu nodes
	nodes := createMockNodeList(6)

	// create 3 pods, all running on different cpu nodes
	pods := []Pod{}
	pods = append(pods, createMockPod(0, "node-0", PodStatusRunning, true))
	pods = append(pods, createMockPod(1, "node-1", PodStatusRunning, true))
	pods = append(pods, createMockPod(2, "node-2", PodStatusRunning, true))

	// Initial, all pods running - should not get any schedule instructions
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 0)

	// In a loop, simulate pod-1 getting stuck in Unknown state, and then recovering on same node
	for i := 1; i <= 5; i++ {
		// Mark pod-1 in Unknown state, and reprocess. We shouldn't get any schedule instructions yet
		pods[1].State = PodStatusUnknown
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
		assert.Equal(t, len(schedulerInstructions), 0)

		// Update the firstHitTime for pod-1 Unknown state to be 10 minutes ago, and reprocess. We should get delete instruction for stuck pod-1
		podState := schedulerState.PodMap["cacheserver-1"]
		podState.FirstHitTimestamp = time.Now().Add(-10 * time.Minute)
		schedulerState.PodMap["cacheserver-1"] = podState
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
		assert.Equal(t, len(schedulerInstructions), 1)
		assertPodDeleted(t, schedulerInstructions, "cacheserver-1", "Pod cacheserver-1 is stuck on node node-1 in Unknown state for more than 5m0s")

		// Mark pod-1 as pending, and reprocess. Pod-1 should get rescheduled on "node-1"
		pods[1].markPodAsPending()
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
		assert.Equal(t, len(schedulerInstructions), 1)
		assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-1", "node-1", "Rescheduling pod on the node it was previously running on")

		// Mark pod-1 as running, and reprocess. We should not get any schedule instructions
		pods[1].markPodAsRunning("node-1")
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
		assert.Equal(t, len(schedulerInstructions), 0)

		// Update the last hit time for pod-1 to be 10 minutes ago, and reprocess.
		// Scheduler should clear pod stuck events for node "node-1"
		podState = schedulerState.PodMap["cacheserver-1"]
		podState.FirstHitTimestamp = time.Now().Add(-10 * time.Minute)
		schedulerState.PodMap["cacheserver-1"] = podState
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
		assert.Equal(t, len(schedulerInstructions), 0)
		assert.Equal(t, getRecentPodStuckEventCount(schedulerState.NodeMap["node-1"]), 0)
	}
}

func TestNodeCordon(t *testing.T) {
	resetSchedulerState()

	// create 4 cpu nodes
	nodes := createMockNodeList(4)

	// create 3 pods, all running on different cpu nodes
	pods := []Pod{}
	pods = append(pods, createMockPod(0, "node-0", PodStatusRunning, true))
	pods = append(pods, createMockPod(1, "node-1", PodStatusRunning, true))
	pods = append(pods, createMockPod(2, "node-2", PodStatusRunning, true))

	// Initial, all pods running - should not get any schedule instructions
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark node-1 as cordoned and pod-1 as pending, and reprocess. Pod-1 should get scheduled on "node-3", different from "node-1" it was running on previously
	nodes[1].Schedulable = false
	pods[1].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-1", "node-3", "Scheduling pod on healthy node")

	// Mark pod-1 as running on "node-3", and reprocess. We should not get any schedule instructions
	pods[1].markPodAsRunning("node-3")
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 0)

	// Uncordon node-1 and mark pod-1 as pending, and reprocess. Pod-1 should get rescheduled on node-3
	nodes[1].Schedulable = true
	pods[1].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-1", "node-3", "Rescheduling pod on the node it was previously running on")
}

func TestInsufficientNodes(t *testing.T) {
	resetSchedulerState()

	// create 2 cpu nodes
	nodes := createMockNodeList(2)

	// create 2 pods, both running on different cpu nodes
	pods := []Pod{}
	pods = append(pods, createMockPod(0, "node-0", PodStatusRunning, true))
	pods = append(pods, createMockPod(1, "node-1", PodStatusRunning, true))

	// Initial, all pods running - should not get any schedule instructions
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 0)

	// Add new pod-2 as pending, and reprocess. We should get an error instruction for pod-2
	pods = append(pods, createMockPod(2, "", PodStatusPending, true))
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 1)
	checkSchedulerInstructionForPod(
		t,
		schedulerInstructions,
		"cacheserver-2",
		BindInstruction,
		nodeList{},
		"No eligible node found to schedule pod cacheserver-2."+
			" SchedulableFreeNodesMatchingAffinity=0 SchedulableFreeNodesMatchingAffinityWithoutErrors=0"+
			" TotalNodes=2 SchedulableNodes=2 SchedulableNodesWithoutPod=0 NodesWithPod=2 GlobalNodesWithPod=2")
}

func TestPodWithSchedulerBypass(t *testing.T) {
	resetSchedulerState()

	// create 4 cpu nodes
	nodes := createMockNodeList(4)

	// create 1 pod in pending state with scheduler bypass
	pods := []Pod{}
	pods = append(pods, createMockPod(0, "", PodStatusPending, true))
	pods[0].SchedulerBypassed = true

	// Process the nodes and pods, should not get any schedule instruction as scheduler is bypassed
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(schedulerInstructions), 0)
}

// test scheduling across multiple namespaces
func TestMultiSchedulerMultiNamespace(t *testing.T) {
	resetSchedulerState()

	clientset := fake.NewSimpleClientset()
	mockLazyInit(clientset)
	InitSchedulerCoordinator()

	// create 5 nodes
	nodes := createMockNodeList(5)

	// create 3 pods in namespace azureml
	pods := []Pod{}
	pods = append(pods, createMockPod(0, "node-0", PodStatusRunning, true))
	pods = append(pods, createMockPod(1, "node-4", PodStatusRunning, true))
	pods = append(pods, createMockPod(2, "", PodStatusPending, true))
	freeCpuNodes := nodeList{"node-1": true, "node-2": true, "node-3": true}
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, true, "azureml")
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cacheserver-2", freeCpuNodes, "Scheduling pod on healthy node")
	freeCpuNodes.remove(getSchedulerInstructionForPod(schedulerInstructions, "cacheserver-2").DestinationNode)
	assert.Equal(t, len(schedulerInstructions), 1)

	//reset all local state data and start new scheduling cycle in different namespace
	resetSchedulerState()
	pods = []Pod{}

	// create 3 pods in namespace cache-server
	pods = append(pods, createMockPod(0, "", PodStatusPending, true))
	pods = append(pods, createMockPod(1, "", PodStatusPending, true))
	pods = append(pods, createMockPod(2, "", PodStatusPending, true))

	schedulerInstructions = SchedulerPeriodicStep(pods, nodes, true, "cache-server")
	assert.Equal(t, len(schedulerInstructions), 3)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cacheserver-0", freeCpuNodes, "Scheduling pod on healthy node")
	freeCpuNodes.remove(getSchedulerInstructionForPod(schedulerInstructions, "cacheserver-0").DestinationNode)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cacheserver-1", freeCpuNodes, "Scheduling pod on healthy node")
	// nodes are sufficient to schedule pods of each namespace but not enough to schedule all from both namespaces combined
	checkSchedulerInstructionForPod(t, schedulerInstructions, "cacheserver-2", BindInstruction, nodeList{}, "No eligible node found to schedule pod cacheserver-2. SchedulableFreeNodesMatchingAffinity=0 SchedulableFreeNodesMatchingAffinityWithoutErrors=0 TotalNodes=5 SchedulableNodes=5 SchedulableNodesWithoutPod=0 NodesWithPod=0 GlobalNodesWithPod=3")

}

// test node locks updating logic during each scheduling cycle
func TestMultiSchedulSyncLocks(t *testing.T) {
	resetSchedulerState()

	clientset := fake.NewSimpleClientset()
	mockLazyInit(clientset)
	InitSchedulerCoordinator()

	// create 5 nodes
	nodes := createMockNodeList(5)

	nestedMap := map[string]PodInfo{
		"node-2": {
			PodName:      "cacheserver-2",
			PodNamespace: "azureml",
		},
		"node-1": {
			PodName:      "cacheserver-0",
			PodNamespace: "cacheserver",
		},
		"node-4": {
			PodName:      "cacheserver-1",
			PodNamespace: "azureml",
		},
	}

	// Serialize the nested map as a JSON string
	nestedMapJSON, err := json.Marshal(nestedMap)
	assert.NilError(t, err)
	configMap := &corev1.ConfigMap{
		ObjectMeta: metav1.ObjectMeta{
			Name:      "node-cacheserver-mapping",
			Namespace: "default",
		},
		Data: map[string]string{
			"NodeCacheServerMapping": string(nestedMapJSON),
		},
	}

	_, err = clientset.CoreV1().ConfigMaps("default").Create(context.TODO(), configMap, metav1.CreateOptions{})
	assert.NilError(t, err)

	// create 3 pods in namespace azureml
	pods := []Pod{}
	pods = append(pods, createMockPod(2, "node-3", PodStatusRunning, true))
	pods = append(pods, createMockPod(1, "node-4", PodStatusRunning, true))
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, true, "azureml")
	assert.Equal(t, len(schedulerInstructions), 0)
	//locked nodes list
	lockedNodes := coordinator.getAllNamespacesLocks()
	assert.Equal(t, len(lockedNodes), 3)
	// node-2 lock to be released as it is not occupied by azureml namespace
	// node-3 lock to be added as it is occupied by azureml namespace
	// node-4 lock to be kept as it is occupied by azureml namespace
	// node-1 lock not to be touched as it is locked by cacheserver namespace
	expectedLockedNodes := map[string]bool{"node-3": true, "node-1": true, "node-4": true}
	assert.DeepEqual(t, lockedNodes, expectedLockedNodes)

	lockedNodeAzureml := coordinator.getNamespaceLocks("azureml")
	assert.Equal(t, len(lockedNodeAzureml), 2)
	expectedLockedNodesAzureml := map[string]bool{"node-3": true, "node-4": true}
	assert.DeepEqual(t, lockedNodeAzureml, expectedLockedNodesAzureml)

	lockedNodeCacheserver := coordinator.getNamespaceLocks("cacheserver")
	assert.Equal(t, len(lockedNodeCacheserver), 1)
	expectedLockedNodesCacheserver := map[string]bool{"node-1": true}
	assert.DeepEqual(t, lockedNodeCacheserver, expectedLockedNodesCacheserver)

}

func TestMultiSchedulerReserveNodeAlreadyOccupied(t *testing.T) {
	resetSchedulerState()

	clientset := fake.NewSimpleClientset()
	mockLazyInit(clientset)

	// Define a custom reaction for the Update method to return an error for a specific node
	clientset.PrependReactor("update", "configmaps", func(action k8stesting.Action) (handled bool, ret runtime.Object, err error) {
		updateAction, ok := action.(k8stesting.UpdateAction)
		if !ok {
			return false, nil, nil
		}

		configMap, ok := updateAction.GetObject().(*v1.ConfigMap)
		if !ok {
			return false, nil, nil
		}

		// Check if the update is for the specific node
		if strings.Contains(configMap.Data["NodeCacheServerMapping"], "node-1") {
			return true, nil, fmt.Errorf("Node already occupied: node-1")
		}

		return false, nil, nil
	})

	// create 3 nodes
	nodes := createMockNodeList(3)

	// create 3 pods in namespace azureml
	var pods []Pod
	pods = append(pods, createMockPod(0, "node-0", PodStatusRunning, true))
	pods = append(pods, createMockPod(1, "node-2", PodStatusRunning, true))
	pods = append(pods, createMockPod(2, "", PodStatusPending, true))

	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, true, "azureml")
	assert.Equal(t, 1, len(schedulerInstructions))
	// Scheduler should not be able to reserve node-1 as it is already occupied
	checkSchedulerInstructionForPod(t, schedulerInstructions, "cacheserver-2", BindInstruction, nodeList{}, "Scheduling pod on healthy node No more schedulable nodes available TotalNodes=3 SchedulableNodes=3 SchedulableNodesWithoutPod=0 NodesWithPod=2 GlobalNodesWithPod=2")
}

func TestMultiSchedulerReserveNodeWithRetry(t *testing.T) {
	clientset := fake.NewSimpleClientset()
	mockLazyInit(clientset)

	// Track the number of update attempts
	updateAttempts := 0

	// Define a custom reaction for the Update method to return a conflict error on the first 3 attempt
	clientset.PrependReactor("update", "configmaps", func(action k8stesting.Action) (handled bool, ret runtime.Object, err error) {
		updateAction, ok := action.(k8stesting.UpdateAction)
		if !ok {
			return false, nil, nil
		}

		configMap, ok := updateAction.GetObject().(*v1.ConfigMap)
		if !ok {
			return false, nil, nil
		}

		// Check if the update is for node-1
		if strings.Contains(configMap.Data["NodeCacheServerMapping"], "node-1") {
			updateAttempts++
			if updateAttempts <= 3 {
				// Return a conflict error on the first three attempt
				return true, nil, errors.NewConflict(schema.GroupResource{Group: "core", Resource: "configmaps"}, configMap.Name, fmt.Errorf("conflict error"))
			}
		}
		// normal update operation.
		return false, nil, nil
	})

	nodes := createMockNodeList(3)

	// create 3 pods in namespace azureml
	var pods []Pod
	pods = append(pods, createMockPod(0, "node-0", PodStatusRunning, true))
	pods = append(pods, createMockPod(1, "node-2", PodStatusRunning, true))
	pods = append(pods, createMockPod(2, "", PodStatusPending, true))

	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, true, "azureml")
	assert.Equal(t, 1, len(schedulerInstructions))
	// Scheduler should be able to reserve node-1 after the conflict error
	checkSchedulerInstructionForPod(t, schedulerInstructions, "cacheserver-2", BindInstruction, nodeList{"node-1": true}, "Scheduling pod on healthy node")
}

// TestExcessPodsCleanup tests the scenario
// where 10 pods were running (cacheserver-0,1,2,3,4,6,8,9,16,22)
// but only 6 were desired.
// This caused a deadlock where cacheserver-5 was pending and couldn't be scheduled.
// Also tests that excess pods are deleted regardless of their state.
func TestExcessPodsCleanup(t *testing.T) {
	resetSchedulerState()
	clientset := fake.NewSimpleClientset()
	mockLazyInit(clientset)

	// Create a mock StatefulSet with 6 desired replicas
	statefulset := createMockStatefulSet("cacheserver", "azureml", 6)
	_, err := clientset.AppsV1().StatefulSets("azureml").Create(context.TODO(), statefulset, metav1.CreateOptions{})
	assert.NilError(t, err)

	// Create 10 nodes (enough for the desired 6 pods)
	nodes := createMockNodeList(10)

	// Simulate the real-world scenario: 10 pods with various states but only 6 desired
	// Running pods: cacheserver-0, 1, 2, 3, 4 (desired, all running)
	// Excess pods in different states:
	// cacheserver-6 (Running), 8 (Terminating), 9 (NotReady), 16 (Failed), 22 (Running)
	pods := []Pod{}
	pods = append(pods, createMockPod(0, "node-0", PodStatusRunning, true))
	pods = append(pods, createMockPod(1, "node-1", PodStatusRunning, true))
	pods = append(pods, createMockPod(2, "node-2", PodStatusRunning, true))
	pods = append(pods, createMockPod(3, "node-3", PodStatusRunning, true))
	pods = append(pods, createMockPod(4, "node-4", PodStatusRunning, true))

	// Excess pods in various states
	pod6 := createMockPod(6, "node-5", PodStatusRunning, true)
	pod6.PodName = "cacheserver-6"
	pods = append(pods, pod6)

	pod8 := createMockPod(8, "node-6", PodStatusTerminating, true)
	pod8.PodName = "cacheserver-8"
	pods = append(pods, pod8)

	pod9 := createMockPod(9, "node-7", PodStatusNotReady, true)
	pod9.PodName = "cacheserver-9"
	pods = append(pods, pod9)

	pod16 := createMockPod(16, "node-8", PodStatusFailed, true)
	pod16.PodName = "cacheserver-16"
	pods = append(pods, pod16)

	pod22 := createMockPod(22, "node-9", PodStatusRunning, true)
	pod22.PodName = "cacheserver-22"
	pods = append(pods, pod22)

	// cacheserver-5 is pending (can't find a free node)
	pendingPod := createMockPod(5, "", PodStatusPending, true)
	pendingPod.PodName = "cacheserver-5"
	pods = append(pods, pendingPod)

	// Run scheduler - should identify excess pods and issue delete instructions
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, false, "azureml")

	// Verify the correct pods are being deleted with proper reason
	assertPodDeleted(t, schedulerInstructions, "cacheserver-6", "Pod cacheserver-6 has index >= desired replica count, cleaning up excess pod")
	assertPodDeleted(t, schedulerInstructions, "cacheserver-8", "Pod cacheserver-8 has index >= desired replica count, cleaning up excess pod")
	assertPodDeleted(t, schedulerInstructions, "cacheserver-9", "Pod cacheserver-9 has index >= desired replica count, cleaning up excess pod")
	assertPodDeleted(t, schedulerInstructions, "cacheserver-16", "Pod cacheserver-16 has index >= desired replica count, cleaning up excess pod")
	assertPodDeleted(t, schedulerInstructions, "cacheserver-22", "Pod cacheserver-22 has index >= desired replica count, cleaning up excess pod")

	// Verify cacheserver-5 is pending and cannot be scheduled due to no available nodes
	pod5Instruction := getSchedulerInstructionForPod(schedulerInstructions, "cacheserver-5")
	assert.Assert(t, pod5Instruction != nil, "Expected bind instruction for cacheserver-5")
	assert.Equal(t, pod5Instruction.Type, BindInstruction)
	assert.Equal(t, pod5Instruction.DestinationNode, "", "cacheserver-5 should not have a destination node in first cycle")
	assert.Assert(t, strings.Contains(pod5Instruction.Reason, "No eligible node found"), "Expected 'No eligible node found' reason for cacheserver-5")

	// Simulate deletion of excess pods by removing them from the pod list
	podsAfterDeletion := []Pod{}
	for _, pod := range pods {
		index := extractPodIndexFromName(pod.PodName)
		if index >= 0 && index < 6 {
			podsAfterDeletion = append(podsAfterDeletion, pod)
		}
	}

	// Run scheduler again - now cacheserver-5 should be schedulable to one of the freed nodes
	schedulerInstructions = SchedulerPeriodicStep(podsAfterDeletion, nodes, false, "azureml")
	freeNodes := nodeList{"node-5": true, "node-6": true, "node-7": true, "node-8": true, "node-9": true}
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cacheserver-5", freeNodes, "Scheduling pod on healthy node")
}

// TestNoExcessPodsWhenDesiredMatches tests that no pods are deleted when
// the desired replica count matches the running pod count and indices
func TestNoExcessPodsWhenDesiredMatches(t *testing.T) {
	resetSchedulerState()
	clientset := fake.NewSimpleClientset()
	mockLazyInit(clientset)

	// Create a mock StatefulSet with 4 desired replicas
	statefulset := createMockStatefulSet("cacheserver", "azureml", 4)
	_, err := clientset.AppsV1().StatefulSets("azureml").Create(context.TODO(), statefulset, metav1.CreateOptions{})
	assert.NilError(t, err)

	nodes := createMockNodeList(4)

	// Running pods: cacheserver-0, 1, 2, 3 (desired configuration)
	pods := []Pod{}
	for i := 0; i < 4; i++ {
		pod := createMockPod(i, fmt.Sprintf("node-%d", i), PodStatusRunning, true)
		pods = append(pods, pod)
	}

	// Run scheduler
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes, false, "azureml")

	// Should NOT issue any delete instructions
	deleteCount := 0
	for _, instruction := range schedulerInstructions {
		if instruction.Type == DeleteInstruction {
			deleteCount++
		}
	}

	assert.Equal(t, deleteCount, 0, "Expected no pods to be deleted when desired matches actual")
}

// TestStickinessRespectsResources verifies that resource constraints take
// precedence over pod stickiness. When a pod restarts with increased resource
// requirements, it should be scheduled to a node with sufficient resources
// rather than sticking to its previous node that lacks capacity.
func TestStickinessRespectsResources(t *testing.T) {
	resetSchedulerState()
	mockLazyInit(fake.NewSimpleClientset())

	// Setup: 3 Nodes
	// Node-0: 0 IB (Small capacity)
	// Node-1: 1 IB (Large capacity, will be occupied)
	// Node-2: 1 IB (Large capacity, will be free)
	nodes := createMockNodeList(3)
	nodes[0].Allocatable["rdma/hca_rdma_infiniband"] = resource.MustParse("0")
	nodes[1].Allocatable["rdma/hca_rdma_infiniband"] = resource.MustParse("1")
	nodes[2].Allocatable["rdma/hca_rdma_infiniband"] = resource.MustParse("1")

	// --- Phase 1: Pod starts with NO IB request ---
	// Occupy node-1 and node-2 with other pods to force pod-0 to node-0
	pod1 := createMockPod(1, "node-1", PodStatusRunning, true)
	pod2 := createMockPod(2, "node-2", PodStatusRunning, true)
	pod := createMockPod(0, "", PodStatusPending, true)
	pods := []Pod{pod1, pod2, pod}

	// Run Scheduler - should schedule to node-0 (only free node)
	instructions := SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(instructions), 1)
	assertPodScheduledToNode(t, instructions, pod.PodName, "node-0", "Scheduling pod on healthy node")

	// Simulate pod running on node-0
	pod.markPodAsRunning("node-0")
	pods = []Pod{pod1, pod2, pod}

	// Run scheduler again to establish stickiness in state
	instructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(instructions), 0) // No new instructions when pod is running

	// Free up node-2 by removing pod-2
	pods = []Pod{pod1, pod}

	// --- Phase 2: Pod restarts with IB request ---
	// Pod now Pending (crashed/recreated) and requests 1 IB.
	pod.markPodAsPending()
	pod.Requests["rdma/hca_rdma_infiniband"] = resource.MustParse("1")
	pods = []Pod{pod1, pod}

	// Run Scheduler again - should schedule to node-2 (resources trump stickiness)
	instructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(instructions), 1)

	// MUST be Node-2 (not node-0 due to stickiness).
	assertPodScheduledToNode(t, instructions, pod.PodName, "node-2", "Scheduling pod on healthy node")
}

// TestStickinessWorksWhenResourcesSufficient verifies that pod stickiness
// functions correctly when the previous node has sufficient resources even
// after the pod's resource requirements increase. This is the positive test
// case confirming stickiness works when resources are adequate.
func TestStickinessWorksWhenResourcesSufficient(t *testing.T) {
	resetSchedulerState()
	mockLazyInit(fake.NewSimpleClientset())

	// Setup: 2 Nodes both with 1 IB
	nodes := createMockNodeList(2)
	nodes[0].Allocatable["rdma/hca_rdma_infiniband"] = resource.MustParse("1")
	nodes[1].Allocatable["rdma/hca_rdma_infiniband"] = resource.MustParse("1")

	// --- Phase 1: Pod starts with NO IB request ---
	// Occupy node-1 to force pod-0 to node-0
	pod1 := createMockPod(1, "node-1", PodStatusRunning, true)
	pod := createMockPod(0, "", PodStatusPending, true)
	pods := []Pod{pod1, pod}

	// Run Scheduler - should schedule to node-0
	instructions := SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(instructions), 1)
	assertPodScheduledToNode(t, instructions, pod.PodName, "node-0", "Scheduling pod on healthy node")

	// Simulate pod running on node-0
	pod.markPodAsRunning("node-0")
	pods = []Pod{pod1, pod}

	// Run scheduler to establish stickiness
	instructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(instructions), 0)

	// --- Phase 2: Pod restarts with 1 IB request ---
	// Pod now requests 1 IB, but node-0 has 1 IB available
	pod.markPodAsPending()
	pod.Requests["rdma/hca_rdma_infiniband"] = resource.MustParse("1")
	pods = []Pod{pod1, pod}

	instructions = SchedulerPeriodicStep(pods, nodes, false, "")
	assert.Equal(t, len(instructions), 1)
	// Should stick to node-0 since it has sufficient resources (1 IB)
	assertPodScheduledToNode(t, instructions, pod.PodName, "node-0", "Rescheduling pod on the node it was previously running on")
}
