package main

import (
	"fmt"
	"testing"
	"time"

	"gotest.tools/v3/assert"
)

func init() {
	initLogger("debug")
}

// helper to create mock node list
func createMockNodeList(nodeCount int) []Node {
	nodes := []Node{}
	for i := 0; i < nodeCount; i++ {
		nodes = append(nodes, Node{
			NodeName:       fmt.Sprintf("node-%d", i),
			Schedulable:    true,
			ProcessingUnit: "cpu",
			Agentpool:      "",
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

	return Pod{
		PodName:                fmt.Sprintf("cacheserver-%d", podIdx),
		NodeName:               nodeName,
		State:                  state,
		SchedulerBypassed:      false,
		AllowedProcessingUnits: allowedProcessingUnits,
		AllowedAgentpools:      []string{},
		UseEphemeralStorage:    false,
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

func TestPodSticksToNode(t *testing.T) {
	resetSchedulerState()

	// create 12 nodes
	nodes := createMockNodeList(12)

	pods := []Pod{}
	pods = append(pods, createMockPod(0, "node-8", PodStatusRunning, true))
	pods = append(pods, createMockPod(1, "node-0", PodStatusRunning, true))
	pods = append(pods, createMockPod(2, "node-10", PodStatusRunning, true))

	// Initial, all pods running - should not get any schedule instructions
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark pod-2 as pending, and reprocess. Pod-2 should get scheduled to node-10
	pods[2].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-2", "node-10", "Rescheduling pod on the node it was previously running on")

	// Mark pod-2 as running again, and reprocess. We should not get any schedule instructions
	pods[2].markPodAsRunning("node-10")
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark pod-1 as pending, and reprocess. Pod-1 should get scheduled to node-0
	pods[1].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-1", "node-0", "Rescheduling pod on the node it was previously running on")

	// Mark pod-1 as running again, and reprocess. We should not get any schedule instructions
	pods[1].markPodAsRunning("node-0")
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark pod-0 as pending, and reprocess. Pod-0 should get scheduled to node-8
	pods[0].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-0", "node-8", "Rescheduling pod on the node it was previously running on")

	// Mark pod-0 as running again, and reprocess. We should not get any schedule instructions
	pods[0].markPodAsRunning("node-8")
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark all 3 pods as pending, and reprocess. We should get 3 schedule instructions with each pod getting scheduled to node it was previously running on
	pods[0].markPodAsPending()
	pods[1].markPodAsPending()
	pods[2].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 3)
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-0", "node-8", "Rescheduling pod on the node it was previously running on")
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-1", "node-0", "Rescheduling pod on the node it was previously running on")
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-2", "node-10", "Rescheduling pod on the node it was previously running on")

	// Mark all 3 pods as running again, and reprocess. We should not get any schedule instructions
	pods[0].markPodAsRunning("node-8")
	pods[1].markPodAsRunning("node-0")
	pods[2].markPodAsRunning("node-10")
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
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
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes)
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
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes)
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
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	for podStuckCount := 1; podStuckCount <= 3; podStuckCount++ {
		// Mark pod-1 in Terminating state, and reprocess. We shouldn't get any schedule instructions yet
		pods[1].State = PodStatusTerminating
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 0)

		// Update the firstHitTime for pod-1 Unknown state to be 10 minutes ago, and reprocess. We should get delete instruction for stuck pod-1
		podState := schedulerState.PodMap["cacheserver-1"]
		podState.FirstHitTimestamp = time.Now().Add(-10 * time.Minute)
		schedulerState.PodMap["cacheserver-1"] = podState
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 1)
		assertPodDeleted(t, schedulerInstructions, "cacheserver-1", "Pod cacheserver-1 is stuck on node node-1 in Terminating state for more than 5m0s")

		// Mark pod-1 as pending, and reprocess. Pod-1 should get rescheduled on "node-1" first 2 times, and then on "node-3" the 3rd time
		pods[1].markPodAsPending()
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
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
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 0)
	}

	// Mark node-3 as cordoned and pod-1 as pending, and reprocess. Pod-1 should get scheduled on "node-1"
	nodes[3].Schedulable = false
	pods[1].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
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
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// In a loop, simulate pod-1 getting stuck in Unknown state, and then recovering on same node
	for i := 1; i <= 5; i++ {
		// Mark pod-1 in Unknown state, and reprocess. We shouldn't get any schedule instructions yet
		pods[1].State = PodStatusUnknown
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 0)

		// Update the firstHitTime for pod-1 Unknown state to be 10 minutes ago, and reprocess. We should get delete instruction for stuck pod-1
		podState := schedulerState.PodMap["cacheserver-1"]
		podState.FirstHitTimestamp = time.Now().Add(-10 * time.Minute)
		schedulerState.PodMap["cacheserver-1"] = podState
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 1)
		assertPodDeleted(t, schedulerInstructions, "cacheserver-1", "Pod cacheserver-1 is stuck on node node-1 in Unknown state for more than 5m0s")

		// Mark pod-1 as pending, and reprocess. Pod-1 should get rescheduled on "node-1"
		pods[1].markPodAsPending()
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 1)
		assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-1", "node-1", "Rescheduling pod on the node it was previously running on")

		// Mark pod-1 as running, and reprocess. We should not get any schedule instructions
		pods[1].markPodAsRunning("node-1")
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 0)

		// Update the last hit time for pod-1 to be 10 minutes ago, and reprocess.
		// Scheduler should clear pod stuck events for node "node-1"
		podState = schedulerState.PodMap["cacheserver-1"]
		podState.FirstHitTimestamp = time.Now().Add(-10 * time.Minute)
		schedulerState.PodMap["cacheserver-1"] = podState
		schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
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
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark node-1 as cordoned and pod-1 as pending, and reprocess. Pod-1 should get scheduled on "node-3", different from "node-1" it was running on previously
	nodes[1].Schedulable = false
	pods[1].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cacheserver-1", "node-3", "Scheduling pod on healthy node")

	// Mark pod-1 as running on "node-3", and reprocess. We should not get any schedule instructions
	pods[1].markPodAsRunning("node-3")
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Uncordon node-1 and mark pod-1 as pending, and reprocess. Pod-1 should get rescheduled on node-3
	nodes[1].Schedulable = true
	pods[1].markPodAsPending()
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
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
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Add new pod-2 as pending, and reprocess. We should get an error instruction for pod-2
	pods = append(pods, createMockPod(2, "", PodStatusPending, true))
	schedulerInstructions = SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	checkSchedulerInstructionForPod(
		t,
		schedulerInstructions,
		"cacheserver-2",
		BindInstruction,
		nodeList{},
		"No eligible node found to schedule pod cacheserver-2."+
			" SchedulableFreeNodesMatchingAffinity=0 SchedulableFreeNodesMatchingAffinityWithoutErrors=0"+
			" TotalNodes=2 SchedulableNodes=2 SchedulableNodesWithoutPod=0 NodesWithPod=2")
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
	schedulerInstructions := SchedulerPeriodicStep(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)
}
