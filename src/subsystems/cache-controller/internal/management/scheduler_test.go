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
	"context"
	"testing"
	"time"

	"gotest.tools/v3/assert"
	"k8s.io/apimachinery/pkg/api/resource"

	config "github.com/Azure/dacs/cache-controller/internal/config"
)

var tracker *Tracker

func init() {
	resetTracker()
}

func getSchedulerInstructionForPod(schedulerInstructions []SchedulerInstruction, namespace string, podName string) *SchedulerInstruction {
	for _, schedulerInstruction := range schedulerInstructions {
		if schedulerInstruction.PodName == podName && schedulerInstruction.Namespace == namespace {
			return &schedulerInstruction
		}
	}

	return nil
}

func checkSchedulerInstructionForPod(
	t *testing.T,
	schedulerInstructions []SchedulerInstruction,
	namespace string,
	podName string,
	expectedType SchedulerInstructionType,
	allowedDestinationNodes nodeList,
	expectedReason string) {

	schedulerInstruction := getSchedulerInstructionForPod(schedulerInstructions, namespace, podName)
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
	namespace string,
	podName string,
	expectedNodeName string,
	expectedReason string) {

	expectedNodeList := map[string]bool{expectedNodeName: true}
	checkSchedulerInstructionForPod(t, schedulerInstructions, namespace, podName, BindInstruction, expectedNodeList, expectedReason)
}

func assertPodScheduledToOneOfNodes(
	t *testing.T,
	schedulerInstructions []SchedulerInstruction,
	namespace string,
	podName string,
	expectedNodeNames nodeList,
	expectedReason string) {

	checkSchedulerInstructionForPod(t, schedulerInstructions, namespace, podName, BindInstruction, expectedNodeNames, expectedReason)
}

func assertPodDeleted(
	t *testing.T,
	schedulerInstructions []SchedulerInstruction,
	namespace string,
	podName string,
	expectedReason string) {

	checkSchedulerInstructionForPod(t, schedulerInstructions, namespace, podName, DeleteInstruction, nodeList{}, expectedReason)
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

func resetTracker() {
	cfg := &config.Config{
		PodStuckThresholdDuration:          5 * time.Minute,
		ShortTermNodeLossThresholdDuration: 10 * time.Minute,
	}
	newTracker := InitTracker(context.Background(), cfg)
	tracker = newTracker
}

// expireShortTermNodeLossWindow backdates the node's UnschedulableSince
// so the scheduler treats it as long-term lost without waiting in real time.
func expireShortTermNodeLossWindow(t *testing.T, nodeName string) {
	t.Helper()
	info, ok := tracker.State.CurrentNodes[nodeName]
	assert.Assert(t, ok, "node %s is not tracked", nodeName)
	info.UnschedulableSince = time.Now().Add(-(tracker.Config.ShortTermNodeLossThresholdDuration + time.Minute))
	tracker.State.CurrentNodes[nodeName] = info
}

func getSchedulerInstructions(pods []Pod, nodes []Node) []SchedulerInstruction {
	return tracker.UpdateAndSchedule(context.TODO(), "test-ns/test-cache", nodes, pods)
}

func TestPodSticksToNode(t *testing.T) {
	resetTracker()

	// create 12 nodes
	nodes := CreateMockGpuNodeList(12)

	pods := []Pod{}
	pods = append(pods, CreateMockPod("cachens", 0, "node-8", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 1, "node-0", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 2, "node-10", PodStatusRunning, true))

	// Initial, all pods running - should not get any schedule instructions
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark pod-2 as pending, and reprocess. Pod-2 should get scheduled to node-10
	pods[2].markPodAsPending()
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(
		t, schedulerInstructions, "cachens", "cacheserver-2", "node-10",
		"Rescheduling pod on the node it was previously running on")

	// Mark pod-2 as running again, and reprocess. We should not get any schedule instructions
	pods[2].markPodAsRunning("node-10")
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark pod-1 as pending, and reprocess. Pod-1 should get scheduled to node-0
	pods[1].markPodAsPending()
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(
		t, schedulerInstructions, "cachens", "cacheserver-1", "node-0",
		"Rescheduling pod on the node it was previously running on")

	// Mark pod-1 as running again, and reprocess. We should not get any schedule instructions
	pods[1].markPodAsRunning("node-0")
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark pod-0 as pending, and reprocess. Pod-0 should get scheduled to node-8
	pods[0].markPodAsPending()
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(
		t, schedulerInstructions, "cachens", "cacheserver-0", "node-8",
		"Rescheduling pod on the node it was previously running on")

	// Mark pod-0 as running again, and reprocess. We should not get any schedule instructions
	pods[0].markPodAsRunning("node-8")
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Mark all 3 pods as pending, and reprocess. We should get 3 schedule instructions with each pod getting scheduled to node it was previously running on
	pods[0].markPodAsPending()
	pods[1].markPodAsPending()
	pods[2].markPodAsPending()
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 3)
	assertPodScheduledToNode(
		t, schedulerInstructions, "cachens", "cacheserver-0", "node-8",
		"Rescheduling pod on the node it was previously running on")
	assertPodScheduledToNode(
		t, schedulerInstructions, "cachens", "cacheserver-1", "node-0",
		"Rescheduling pod on the node it was previously running on")
	assertPodScheduledToNode(
		t, schedulerInstructions, "cachens", "cacheserver-2", "node-10",
		"Rescheduling pod on the node it was previously running on")
	// Mark all 3 pods as running again, and reprocess. We should not get any schedule instructions
	pods[0].markPodAsRunning("node-8")
	pods[1].markPodAsRunning("node-0")
	pods[2].markPodAsRunning("node-10")
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)
}

func TestPodMovesForLongTermNodeLoss(t *testing.T) {
	resetTracker()

	// create 6 nodes
	nodes := CreateMockGpuNodeList(6)

	// create 3 pods, all running on different nodes
	pods := []Pod{}
	pods = append(pods, CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 1, "node-1", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 2, "node-2", PodStatusRunning, true))

	freeNodes := nodeList{"node-3": true, "node-4": true, "node-5": true}

	// Initial, all pods running - should not get any schedule instructions
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Remove node-1 from the nodes list, mark pod-1 as pending, and reprocess. Pod-1 should get scheduled to a different node as node-1 is removed
	nodes = append(nodes[:1], nodes[2:]...)
	pods[1].markPodAsPending()
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cachens", "cacheserver-1", freeNodes, "Scheduling pod on healthy node")
	newNodeForPod1 := getSchedulerInstructionForPod(schedulerInstructions, "cachens", "cacheserver-1").DestinationNode
	freeNodes.remove(newNodeForPod1)

	// Mark pod-1 as running again, and reprocess. We should not get any schedule instructions
	pods[1].markPodAsRunning(newNodeForPod1)
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Cordon node-0, mark pod-0 as pending, and reprocess. Pod-0 should get scheduled to a different node as node-0 is cordoned
	nodes[0].Schedulable = false
	pods[0].markPodAsPending()

	// node-0 is still within the short-term node loss window, so pod-0 is held
	// Pending and no scheduler instruction is emitted yet.
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Push node-0 past the short-term window so it's treated as long-term node
	// loss. Pod-0 falls back to a different healthy node.
	expireShortTermNodeLossWindow(t, "node-0")

	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cachens", "cacheserver-0", freeNodes, "Scheduling pod on healthy node")
	newNodeForPod0 := getSchedulerInstructionForPod(schedulerInstructions, "cachens", "cacheserver-0").DestinationNode
	freeNodes.remove(newNodeForPod0)

	// Mark pod-0 as running again, and reprocess. We should not get any schedule instructions
	pods[0].markPodAsRunning(newNodeForPod0)
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)
}

func TestPodAffinityHonoredProcessingUnit(t *testing.T) {
	resetTracker()

	// create 6 nodes, 3 gpu (node-0, node-1, node-2) + 3 cpu (node-3, node-4, node-5)
	nodes := CreateMockGpuNodeList(6)
	nodes[3].NodeLabels["dacs.azure.com/processing-unit"] = "cpu"
	nodes[4].NodeLabels["dacs.azure.com/processing-unit"] = "cpu"
	nodes[5].NodeLabels["dacs.azure.com/processing-unit"] = "cpu"

	// create 3 pending pods, all with node affinity for gpu
	pods := []Pod{}
	pods = append(pods, CreateMockPod("cachens", 0, "", PodStatusPending, true))
	pods = append(pods, CreateMockPod("cachens", 1, "", PodStatusPending, true))
	pods = append(pods, CreateMockPod("cachens", 2, "", PodStatusPending, true))

	// Initial, all pods pending - should get scheduled to gpu nodes
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 3)
	// Pod 0 should be scheduled to one of the 3 gpu nodes
	freeGpuNodes := nodeList{"node-0": true, "node-1": true, "node-2": true}
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cachens", "cacheserver-0", freeGpuNodes, "Scheduling pod on healthy node")
	// Pod 1 should be scheduled to one of the 3 cpu nodes, excluding the node pod 0 was scheduled to
	freeGpuNodes.remove(getSchedulerInstructionForPod(schedulerInstructions, "cachens", "cacheserver-0").DestinationNode)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cachens", "cacheserver-1", freeGpuNodes, "Scheduling pod on healthy node")
	// Pod 2 should be scheduled to one of the 3 cpu nodes, excl	uding the nodes pod 0 and 1 were scheduled to
	freeGpuNodes.remove(getSchedulerInstructionForPod(schedulerInstructions, "cachens", "cacheserver-1").DestinationNode)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cachens", "cacheserver-2", freeGpuNodes, "Scheduling pod on healthy node")
}

func TestPodAffinityHonoredAgentpool(t *testing.T) {
	resetTracker()
	// create 6 nodes, 3 with agentpool "system" and 3 with agentpool "user"
	nodes := CreateMockNodeList(6, "", "system")
	nodes[3].NodeLabels["agentpool"] = "user"
	nodes[4].NodeLabels["agentpool"] = "user"
	nodes[5].NodeLabels["agentpool"] = "user"

	// create 3 pending pods, all with node affinity for agentpool "user"
	pods := []Pod{}
	pods = append(pods, CreateMockPod("cachens", 0, "", PodStatusPending, true))
	pods = append(pods, CreateMockPod("cachens", 1, "", PodStatusPending, true))
	pods = append(pods, CreateMockPod("cachens", 2, "", PodStatusPending, true))
	pods[0].NodeSelectorKey = "agentpool"
	pods[0].NodeSelectorValue = "user"
	pods[1].NodeSelectorKey = "agentpool"
	pods[1].NodeSelectorValue = "user"
	pods[2].NodeSelectorKey = "agentpool"
	pods[2].NodeSelectorValue = "user"

	// Initial, all pods pending - should get scheduled to nodes with agentpool "user"
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 3)
	// Pod 0 should be scheduled to one of the 3 nodes with agentpool "user"
	freeUserNodes := nodeList{"node-3": true, "node-4": true, "node-5": true}
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cachens", "cacheserver-0", freeUserNodes, "Scheduling pod on healthy node")
	// Pod 1 should be scheduled to one of the 3 nodes with agentpool "user", excluding the node pod 0 was scheduled to
	freeUserNodes.remove(getSchedulerInstructionForPod(schedulerInstructions, "cachens", "cacheserver-0").DestinationNode)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cachens", "cacheserver-1", freeUserNodes, "Scheduling pod on healthy node")
	// Pod 2 should be scheduled to one of the 3 nodes with agentpool "user", excluding the nodes pod 0 and 1 were scheduled to
	freeUserNodes.remove(getSchedulerInstructionForPod(schedulerInstructions, "cachens", "cacheserver-1").DestinationNode)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cachens", "cacheserver-2", freeUserNodes, "Scheduling pod on healthy node")
}

func TestAllPodsEvictedWhenNodeNoLongerMatchesAffinity(t *testing.T) {
	resetTracker()

	podStates := []PodStatus{
		PodStatusRunning,
		PodStatusPending,
		PodStatusNotReady,
		PodStatusFailed,
		PodStatusUnknown,
		PodStatusSucceeded,
		PodStatusTerminating,
	}
	nodes := CreateMockGpuNodeList(len(podStates))
	pods := make([]Pod, 0, len(podStates))
	for i, state := range podStates {
		pods = append(pods, CreateMockPod("cachens", i, nodes[i].NodeName, state, true))
	}

	assert.Equal(t, len(getSchedulerInstructions(pods, nodes)), 0)

	for i := range nodes {
		nodes[i].NodeLabels["dacs.azure.com/processing-unit"] = "cpu"
	}

	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), len(podStates)-1)
	for _, pod := range pods[:len(pods)-1] {
		assertPodDeleted(
			t,
			schedulerInstructions,
			pod.Namespace,
			pod.PodName,
			"Node "+pod.NodeName+" no longer matches pod affinity dacs.azure.com/processing-unit=gpu",
		)
	}
	terminatingPod := pods[len(pods)-1]
	assert.Assert(
		t,
		getSchedulerInstructionForPod(schedulerInstructions, terminatingPod.Namespace, terminatingPod.PodName) == nil,
		"terminating pod should not receive another delete instruction",
	)
}

func TestSchedulerBypassedPodNotEvicted(t *testing.T) {
	resetTracker()

	nodes := CreateMockGpuNodeList(2)
	nodes[0].NodeLabels["dacs.azure.com/processing-unit"] = "cpu"
	pod := CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true)
	pod.SchedulerBypassed = true

	assert.Equal(t, len(getSchedulerInstructions([]Pod{pod}, nodes)), 0)
}

func TestPodNotEvictedWhenCurrentNodeIsMissing(t *testing.T) {
	resetTracker()

	nodes := CreateMockGpuNodeList(1)
	nodes[0].NodeName = "node-1"
	pod := CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true)

	assert.Equal(t, len(getSchedulerInstructions([]Pod{pod}, nodes)), 0)
}

func TestContinousStuckPod(t *testing.T) {
	resetTracker()

	// create 4 gpu nodes
	nodes := CreateMockGpuNodeList(4)

	// create 3 pods, all running on different gpu nodes
	pods := []Pod{}
	pods = append(pods, CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 1, "node-1", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 2, "node-2", PodStatusRunning, true))

	// Initial, all pods running - should not get any schedule instructions
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	for podStuckCount := 1; podStuckCount <= 3; podStuckCount++ {
		// Mark pod-1 in Terminating state, and reprocess. We shouldn't get any schedule instructions yet
		pods[1].State = PodStatusTerminating
		schedulerInstructions = getSchedulerInstructions(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 0)

		// Update the firstHitTime for pod-1 Unknown state to be 10 minutes ago, and reprocess.
		// We should get delete instruction for stuck pod-1
		podState := tracker.State.PodMap["cachens"]["cacheserver-1"]
		podState.FirstHitTimestamp = time.Now().Add(-10 * time.Minute)
		tracker.State.PodMap["cachens"]["cacheserver-1"] = podState
		schedulerInstructions = getSchedulerInstructions(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 1)
		assertPodDeleted(
			t, schedulerInstructions, "cachens", "cacheserver-1",
			"Pod cacheserver-1(cachens) is stuck on node node-1 in Terminating state for more than 5m0s")

		// Mark pod-1 as pending, and reprocess.
		// Pod-1 should get rescheduled on "node-1" first 2 times, and then on "node-3" the 3rd time
		pods[1].markPodAsPending()
		schedulerInstructions = getSchedulerInstructions(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 1)
		expectedDestinationNode := "node-1"
		expectedReason := "Rescheduling pod on the node it was previously running on"
		if podStuckCount == 3 {
			expectedDestinationNode = "node-3"
			expectedReason = "Scheduling pod on healthy node"
		}
		assertPodScheduledToNode(t, schedulerInstructions, "cachens", "cacheserver-1", expectedDestinationNode, expectedReason)
		// Mark pod-1 as running, and reprocess. We should not get any schedule instructions
		pods[1].markPodAsRunning(expectedDestinationNode)
		schedulerInstructions = getSchedulerInstructions(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 0)
	}

	// Cordon node-3 and mark pod-1 pending.
	nodes[3].Schedulable = false
	pods[1].markPodAsPending()

	// No scheduler instruction emitted as the node-3 is still within the short-term loss window.
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// node-3 is now past the short-term loss window and treated as long-term lost.
	// Pod-1 should be scheduled to node-1 even though it has recent errors.
	expireShortTermNodeLossWindow(t, "node-3")
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(
		t, schedulerInstructions, "cachens", "cacheserver-1", "node-1",
		"Scheduling pod on healthy node (warning: node has recent cacheserver errors)")
}

func TestTransientStuckPod(t *testing.T) {
	resetTracker()

	// create 6 Gpu nodes
	nodes := CreateMockGpuNodeList(6)

	// create 3 pods, all running on different gpu nodes
	pods := []Pod{}
	pods = append(pods, CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 1, "node-1", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 2, "node-2", PodStatusRunning, true))

	// Initial, all pods running - should not get any schedule instructions
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// In a loop, simulate pod-1 getting stuck in Unknown state, and then recovering on same node
	for i := 1; i <= 5; i++ {
		// Mark pod-1 in Unknown state, and reprocess. We shouldn't get any schedule instructions yet
		pods[1].State = PodStatusUnknown
		schedulerInstructions = getSchedulerInstructions(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 0)

		// Update the firstHitTime for pod-1 Unknown state to be 10 minutes ago, and reprocess.
		// We should get delete instruction for stuck pod-1
		podState := tracker.State.PodMap["cachens"]["cacheserver-1"]
		podState.FirstHitTimestamp = time.Now().Add(-10 * time.Minute)
		tracker.State.PodMap["cachens"]["cacheserver-1"] = podState
		schedulerInstructions = getSchedulerInstructions(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 1)
		assertPodDeleted(t, schedulerInstructions, "cachens", "cacheserver-1",
			"Pod cacheserver-1(cachens) is stuck on node node-1 in Unknown state for more than 5m0s")

		// Mark pod-1 as pending, and reprocess. Pod-1 should get rescheduled on "node-1"
		pods[1].markPodAsPending()
		schedulerInstructions = getSchedulerInstructions(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 1)
		assertPodScheduledToNode(t, schedulerInstructions, "cachens", "cacheserver-1", "node-1",
			"Rescheduling pod on the node it was previously running on")

		// Mark pod-1 as running, and reprocess. We should not get any schedule instructions
		pods[1].markPodAsRunning("node-1")
		schedulerInstructions = getSchedulerInstructions(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 0)

		// Update the last hit time for pod-1 to be 10 minutes ago, and reprocess.
		// Scheduler should clear pod stuck events for node "node-1"
		trackerNode1Info := tracker.State.CurrentNodes["node-1"]
		assert.Equal(t, getRecentPodStuckEventCount(&trackerNode1Info), 1)

		podState = tracker.State.PodMap["cachens"]["cacheserver-1"]
		podState.FirstHitTimestamp = time.Now().Add(-10 * time.Minute)
		tracker.State.PodMap["cachens"]["cacheserver-1"] = podState
		schedulerInstructions = getSchedulerInstructions(pods, nodes)
		assert.Equal(t, len(schedulerInstructions), 0)

		trackerNode1Info = tracker.State.CurrentNodes["node-1"]
		assert.Equal(t, getRecentPodStuckEventCount(&trackerNode1Info), 0)
	}
}

func TestPodMovesAfterLongTermCordon(t *testing.T) {
	resetTracker()

	// create 4 gpu nodes
	nodes := CreateMockGpuNodeList(4)

	// create 3 pods, all running on different gpu nodes
	pods := []Pod{}
	pods = append(pods, CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 1, "node-1", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 2, "node-2", PodStatusRunning, true))

	// Initial, all pods running - should not get any schedule instructions
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Cordon node-1 and mark pod-1 pending
	nodes[1].Schedulable = false
	pods[1].markPodAsPending()

	// No scheduler instruction emitted as node-1 is still within the short-term loss window.
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// node-3 is now past the short-term loss window and treated as long-term lost.
	// pod-1 should now move to a different healthy node.
	expireShortTermNodeLossWindow(t, "node-1")

	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cachens", "cacheserver-1", "node-3", "Scheduling pod on healthy node")

	// Mark pod-1 as running on "node-3", and reprocess. We should not get any schedule instructions
	pods[1].markPodAsRunning("node-3")
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Uncordon node-1 and mark pod-1 as pending, and reprocess. Pod-1 should get rescheduled on node-3
	nodes[1].Schedulable = true
	pods[1].markPodAsPending()
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cachens", "cacheserver-1", "node-3",
		"Rescheduling pod on the node it was previously running on")
}

func TestInsufficientNodes(t *testing.T) {
	resetTracker()

	// create 2 gpu nodes
	nodes := CreateMockGpuNodeList(2)

	// create 2 pods, both running on different gpu nodes
	pods := []Pod{}
	pods = append(pods, CreateMockPod("cachens", 0, "node-1", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 1, "node-0", PodStatusRunning, true))

	// Initial, all pods running - should not get any schedule instructions
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Add new pod-2 as pending, and reprocess. We should get an error instruction for pod-2
	pods = append(pods, CreateMockPod("cachens", 2, "", PodStatusPending, true))
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	checkSchedulerInstructionForPod(
		t,
		schedulerInstructions,
		"cachens",
		"cacheserver-2",
		BindInstruction,
		nodeList{},
		"No eligible node found to schedule pod cacheserver-2."+
			" totalNodes=2, schedulableNodesMatchingAffinity=2, schedulableFreeNodesMatchingAffinity=0,"+
			" occupiedNodes=2")
}

func TestPodWithSchedulerBypass(t *testing.T) {
	resetTracker()

	// create 4 gpu nodes
	nodes := CreateMockGpuNodeList(4)

	// create 1 pod in pending state with scheduler bypass
	pods := []Pod{}
	pods = append(pods, CreateMockPod("cachens", 0, "", PodStatusPending, true))
	pods[0].SchedulerBypassed = true

	// Process the nodes and pods, should not get any schedule instruction as scheduler is bypassed
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)
}

// test scheduling across multiple namespaces
func TestMultiSchedulerMultiNamespace(t *testing.T) {
	resetTracker()

	// create 5 nodes
	nodes := CreateMockGpuNodeList(5)

	// create 3 pods in namespace azureml
	pods := []Pod{}
	pods = append(pods, CreateMockPod("azureml", 0, "node-0", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("azureml", 1, "node-4", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("azureml", 2, "", PodStatusPending, true))
	freeGpuNodes := nodeList{"node-1": true, "node-2": true, "node-3": true}
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "azureml", "cacheserver-2", freeGpuNodes, "Scheduling pod on healthy node")
	freeGpuNodes.remove(getSchedulerInstructionForPod(schedulerInstructions, "azureml", "cacheserver-2").DestinationNode)
	assert.Equal(t, len(schedulerInstructions), 1)

	// create 3 pods in namespace cache-server
	pods[2].markPodAsRunning(getSchedulerInstructionForPod(schedulerInstructions, "azureml", "cacheserver-2").DestinationNode)
	pods = append(pods, CreateMockPod("cache-server", 0, "", PodStatusPending, true))
	pods = append(pods, CreateMockPod("cache-server", 1, "", PodStatusPending, true))
	pods = append(pods, CreateMockPod("cache-server", 2, "", PodStatusPending, true))

	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 3)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cache-server", "cacheserver-0", freeGpuNodes, "Scheduling pod on healthy node")
	freeGpuNodes.remove(getSchedulerInstructionForPod(schedulerInstructions, "cache-server", "cacheserver-0").DestinationNode)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cache-server", "cacheserver-1", freeGpuNodes, "Scheduling pod on healthy node")
	// nodes are sufficient to schedule pods of each namespace but not enough to schedule all from both namespaces combined
	checkSchedulerInstructionForPod(
		t,
		schedulerInstructions,
		"cache-server",
		"cacheserver-2",
		BindInstruction,
		nodeList{},
		"No eligible node found to schedule pod cacheserver-2. totalNodes=5, schedulableNodesMatchingAffinity=5, schedulableFreeNodesMatchingAffinity=0, occupiedNodes=5")
}

// TestStickinessRespectsResources verifies that resource constraints take
// precedence over pod stickiness. When a pod restarts with increased resource
// requirements, it should be scheduled to a node with sufficient resources
// rather than sticking to its previous node that lacks capacity.
func TestStickinessRespectsResources(t *testing.T) {
	resetTracker()

	// Setup: 3 Nodes
	// Node-0: 0 IB (Small capacity)
	// Node-1: 1 IB (Large capacity, will be occupied)
	// Node-2: 1 IB (Large capacity, will be free)
	nodes := CreateMockGpuNodeList(3)
	nodes[0].Allocatable["rdma/hca_rdma_infiniband"] = resource.MustParse("0")

	// Initial setup: All pods running
	// Pod-0 running on node-0 (has no IB requirement yet)
	// Pod-1 running on node-1
	// Pod-2 running on node-2
	pods := []Pod{
		CreateMockPod("azureml", 0, "node-0", PodStatusRunning, true),
		CreateMockPod("azureml", 1, "node-1", PodStatusRunning, true),
		CreateMockPod("azureml", 2, "node-2", PodStatusRunning, true),
	}

	// Run scheduler to register pods in tracker
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Now pod-0 restarts with IB requirement, pod-2 finishes
	pods[0] = CreateMockPod("azureml", 0, "", PodStatusPending, true)
	pods[0].Requests["rdma/hca_rdma_infiniband"] = resource.MustParse("1")
	pods = pods[:2] // Remove pod-2, node-2 is now free

	// Run Scheduler - should schedule to node-2 (has IB), NOT node-0 (lacks IB despite stickiness)
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assertPodScheduledToNode(t, schedulerInstructions, "azureml", "cacheserver-0", "node-2", "Scheduling pod on healthy node")
}

// TestStickinessWorksWhenResourcesSufficient verifies that stickiness still works
// when the previous node has sufficient resources for the restarting pod.
func TestStickinessWorksWhenResourcesSufficient(t *testing.T) {
	resetTracker()

	// Setup: 2 Nodes with equal capacity
	nodes := CreateMockGpuNodeList(2)

	// Initial setup: Both pods running
	pods := []Pod{
		CreateMockPod("azureml", 0, "node-0", PodStatusRunning, true),
		CreateMockPod("azureml", 1, "node-1", PodStatusRunning, true),
	}

	// Run scheduler to register pods in tracker
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Now pod-0 restarts, pod-1 finishes - both nodes are free
	pods[0] = CreateMockPod("azureml", 0, "", PodStatusPending, true)
	pods = pods[:1] // Remove pod-1

	// Run Scheduler - should stick to node-0 since resources are sufficient
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assertPodScheduledToNode(t, schedulerInstructions, "azureml", "cacheserver-0", "node-0",
		"Rescheduling pod on the node it was previously running on")
}

// TestUpdateNodeMap_DiscoversCordonedNodeAsShortTerm covers what happens when
// the tracker sees a node for the first time and that node is already
// cordoned. This is the post controller restart view during an AKS rolling
// upgrade. The tracker has no record of when the cordon happened, so it should
// stamp UnschedulableSince to now and treat the node as short term lost.
func TestUpdateNodeMap_DiscoversCordonedNodeAsShortTerm(t *testing.T) {
	resetTracker()

	// Two new nodes, one cordoned and one healthy.
	nodes := CreateMockGpuNodeList(2)
	nodes[0].Schedulable = false
	nodes[1].Schedulable = true

	before := time.Now()
	_ = getSchedulerInstructions(nil, nodes)
	after := time.Now()

	// Cordoned new node should have UnschedulableSince stamped during the call.
	info0, ok := tracker.State.CurrentNodes["node-0"]
	assert.Assert(t, ok, "node-0 should be tracked")
	assert.Assert(t, !info0.UnschedulableSince.IsZero(),
		"cordoned new node must have a non zero UnschedulableSince")
	assert.Assert(t, !info0.UnschedulableSince.Before(before))
	assert.Assert(t, !info0.UnschedulableSince.After(after))

	// Healthy new node should not have UnschedulableSince populated.
	info1, ok := tracker.State.CurrentNodes["node-1"]
	assert.Assert(t, ok, "node-1 should be tracked")
	assert.Assert(t, info1.UnschedulableSince.IsZero(),
		"healthy new node must have a zero UnschedulableSince")

	// Node-0 should be classified short term and node-1 should not.
	assert.Assert(t, tracker.isNodeLossShortTerm("node-0"))
	assert.Assert(t, !tracker.isNodeLossShortTerm("node-1"))

	// Once the short term window elapses node-0 should flip to long term.
	expireShortTermNodeLossWindow(t, "node-0")
	assert.Assert(t, !tracker.isNodeLossShortTerm("node-0"))
}

// TestPodStickinessForShortTermNodeLoss covers the happy path of short-term node
// loss. A pod whose node goes briefly unschedulable is held Pending instead of
// being moved, other pending pods can still be placed on healthy nodes, and when
// the original node returns the held pod is scheduled back to it.
func TestPodStickinessForShortTermNodeLoss(t *testing.T) {
	resetTracker()

	// create 3 nodes
	nodes := CreateMockGpuNodeList(3)

	// create 1 pod running on node-0
	pods := []Pod{}
	pods = append(pods, CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true))

	// Initially pod is running. So, should not get any schedule instructions
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Cordon node-0, mark pod-0 as pending, and reprocess.
	// Pod-0 should be held pending with no schedule instruction as node-0 is within short-term node loss window
	nodes[0].Schedulable = false
	pods[0].markPodAsPending()
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// New pods added; they should be scheduled onto the other healthy nodes
	// while pod-0 stays Pending on node-0.
	pods = append(pods, CreateMockPod("cachens", 1, "", PodStatusPending, true))
	pods = append(pods, CreateMockPod("cachens", 2, "", PodStatusPending, true))
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 2)

	freeNodes := nodeList{"node-1": true, "node-2": true}
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cachens", "cacheserver-1", freeNodes, "Scheduling pod on healthy node")
	pod1Node := getSchedulerInstructionForPod(schedulerInstructions, "cachens", "cacheserver-1").DestinationNode
	freeNodes.remove(pod1Node)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cachens", "cacheserver-2", freeNodes, "Scheduling pod on healthy node")
	pod2Node := getSchedulerInstructionForPod(schedulerInstructions, "cachens", "cacheserver-2").DestinationNode

	// Mark the newly scheduled pods as running so they aren't re-scheduled in
	// the next reconcile.
	pods[1].markPodAsRunning(pod1Node)
	pods[2].markPodAsRunning(pod2Node)

	// Uncordon node-0. Pod-0 should now bind back to its original node.
	nodes[0].Schedulable = true
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cachens", "cacheserver-0", "node-0",
		"Rescheduling pod on the node it was previously running on")

	// Mark pod-0 as running again, and reprocess. We should not get any schedule instructions
	pods[0].markPodAsRunning("node-0")
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)
}

// TestPodMovesAfterWaitedNodeRemoved verifies that when a held pod's previous
// node is removed from the cluster, the tracker keeps the entry and the
// scheduler keeps holding the pod while inside the short-term loss window.
// Only after the window expires is the entry dropped and the pod moved to a
// healthy node.
func TestPodMovesAfterWaitedNodeRemoved(t *testing.T) {
	resetTracker()

	nodes := CreateMockGpuNodeList(3)

	pods := []Pod{}
	pods = append(pods, CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true))

	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// node-0 is cordoned and pod-0 goes Pending. node-0 is within the short-term node loss window,
	// so scheduler holds pod-0 and emits no instructions.
	nodes[0].Schedulable = false
	pods[0].markPodAsPending()

	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// A subsequent reconcile while node-0 is still cordoned should not change
	// anything. pod-0 stays Pending, waiting for node-0 to return.
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Now node-0 is removed from the cluster entirely (e.g. AKS surge upgrade
	// deleted the VM). The tracker must keep the entry and the scheduler must
	// keep holding pod-0 while still inside the short-term loss window.
	nodes = append(nodes[:0], nodes[1:]...)
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)
	_, stillTracked := tracker.State.CurrentNodes["node-0"]
	assert.Assert(t, stillTracked, "node-0 entry must be retained while inside short-term loss window")

	// After the window expires, the entry is dropped and pod-0 is moved to a
	// healthy node.
	expireShortTermNodeLossWindow(t, "node-0")
	freeNodes := nodeList{"node-1": true, "node-2": true}

	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "cachens", "cacheserver-0", freeNodes,
		"Scheduling pod on healthy node")
	_, stillTrackedAfter := tracker.State.CurrentNodes["node-0"]
	assert.Assert(t, !stillTrackedAfter, "node-0 entry must be removed after short-term window expires")
}

// During an AKS rolling upgrade, multiple nodes can be cordoned within a short
// window. Each pod that was running on a cordoned node should be held Pending
// independently rather than reshuffled. No bind instructions should be emitted
// while every previous node is still within the short-term window.
func TestMultiplePodsHeldDuringRollingUpgrade(t *testing.T) {
	resetTracker()

	nodes := CreateMockGpuNodeList(5)

	pods := []Pod{}
	pods = append(pods, CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 1, "node-1", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("cachens", 2, "node-2", PodStatusRunning, true))

	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Three nodes drain at the same time, like during a rolling upgrade.
	// All three pods go Pending. Their previous nodes are still within the
	// short-term node loss window, so the scheduler holds all three and emits no instructions.
	nodes[0].Schedulable = false
	nodes[1].Schedulable = false
	nodes[2].Schedulable = false
	pods[0].markPodAsPending()
	pods[1].markPodAsPending()
	pods[2].markPodAsPending()

	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Reconcile again before any node comes back. The pods are still Pending. The
	// scheduler does not move them onto the two still-healthy nodes
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// As each node comes back, the corresponding pod binds back to it.
	nodes[0].Schedulable = true
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 1)
	assertPodScheduledToNode(t, schedulerInstructions, "cachens", "cacheserver-0", "node-0",
		"Rescheduling pod on the node it was previously running on")
	pods[0].markPodAsRunning("node-0")

	nodes[1].Schedulable = true
	nodes[2].Schedulable = true
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 2)
	assertPodScheduledToNode(t, schedulerInstructions, "cachens", "cacheserver-1", "node-1",
		"Rescheduling pod on the node it was previously running on")
	assertPodScheduledToNode(t, schedulerInstructions, "cachens", "cacheserver-2", "node-2",
		"Rescheduling pod on the node it was previously running on")
}

// cacheKey used by getSchedulerInstructions; CountPodsAwaitingNodeRecovery is keyed the same way.
const testCacheKey = "test-ns/test-cache"

// When a pod's node was just cordoned, CountPodsAwaitingNodeRecovery should count
// that pod so the controller can mark the PodsScheduled condition as waiting.
func TestCountPodsAwaitingNodeRecovery_PodWaitingOnShortTermLostNode(t *testing.T) {
	resetTracker()

	nodes := CreateMockGpuNodeList(3)
	pods := []Pod{CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true)}

	// pod-0 running on node-0.
	schedulerInstructions := getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	// Cordon node-0. The scheduler now holds pod-0 Pending.
	nodes[0].Schedulable = false
	pods[0].markPodAsPending()
	schedulerInstructions = getSchedulerInstructions(pods, nodes)
	assert.Equal(t, len(schedulerInstructions), 0)

	assert.Equal(t, tracker.CountPodsAwaitingNodeRecovery(testCacheKey), 1)
}

// Once the short-term node loss window has expired, CountPodsAwaitingNodeRecovery no longer
// counts the pod. The scheduler has given up holding it and will place it
// on another node, so the controller should mark it as scheduled or stuck,
// not waiting.
func TestCountPodsAwaitingNodeRecovery_WindowExpired(t *testing.T) {
	resetTracker()

	nodes := CreateMockGpuNodeList(3)
	pods := []Pod{CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true)}

	getSchedulerInstructions(pods, nodes)

	nodes[0].Schedulable = false
	pods[0].markPodAsPending()
	getSchedulerInstructions(pods, nodes)

	// Pod is marked as waiting inside the short-term node loss window.
	assert.Equal(t, tracker.CountPodsAwaitingNodeRecovery(testCacheKey), 1)

	// Push the cordon past the short-term node loss window; the wait should end.
	expireShortTermNodeLossWindow(t, "node-0")
	getSchedulerInstructions(pods, nodes)

	assert.Equal(t, tracker.CountPodsAwaitingNodeRecovery(testCacheKey), 0)
}

// A pod is in waiting state because its node was just cordoned. If that node
// is then removed from the cluster while still inside the short-term loss
// window (e.g. AKS surge upgrade deleted the VM), the tracker keeps the entry
// and the pod must keep being reported as waiting. Only after the window
// expires should CountPodsAwaitingNodeRecovery drop it.
func TestCountPodsAwaitingNodeRecovery_NodeRemovedDuringWait(t *testing.T) {
	resetTracker()

	nodes := CreateMockGpuNodeList(3)
	pods := []Pod{CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true)}

	getSchedulerInstructions(pods, nodes)

	nodes[0].Schedulable = false
	pods[0].markPodAsPending()
	getSchedulerInstructions(pods, nodes)
	assert.Equal(t, tracker.CountPodsAwaitingNodeRecovery(testCacheKey), 1)

	// node-0 is removed from the cluster while still inside the window.
	nodes = append(nodes[:0], nodes[1:]...)
	getSchedulerInstructions(pods, nodes)
	assert.Equal(t, tracker.CountPodsAwaitingNodeRecovery(testCacheKey), 1)

	// Push past the window. The entry is dropped and the pod is no longer waiting.
	expireShortTermNodeLossWindow(t, "node-0")
	getSchedulerInstructions(pods, nodes)
	assert.Equal(t, tracker.CountPodsAwaitingNodeRecovery(testCacheKey), 0)
}

// A Pending pod that has never run before is not in a waiting state; it's
// just an ordinary new pod awaiting scheduling. CountPodsAwaitingNodeRecovery should not
// count it. Classifying it as waiting would misrepresent why PodsScheduled
// is False.
func TestCountPodsAwaitingNodeRecovery_FreshPendingPodNotWaiting(t *testing.T) {
	resetTracker()

	nodes := CreateMockGpuNodeList(3)
	// Cordon one node so the cluster has a node in short-term node loss state;
	nodes[0].Schedulable = false

	// A brand-new pod that has never been bound to a node.
	pods := []Pod{CreateMockPod("cachens", 0, "", PodStatusPending, true)}
	getSchedulerInstructions(pods, nodes)

	assert.Equal(t, tracker.CountPodsAwaitingNodeRecovery(testCacheKey), 0)
}

// A waiting pod must not be counted as stuck. GetPodCountInStuckStates drives
// the PodsScheduled reason in the reconciler. If a waiting pod slipped into
// the stuck count, the controller would set PodsStuck instead of
// PodsWaitingForNodeRecovery, and the wait state would be invisible to operators.
func TestGetPodCountInStuckStates_ExcludesWaitingPod(t *testing.T) {
	resetTracker()

	nodes := CreateMockGpuNodeList(3)
	pods := []Pod{CreateMockPod("cachens", 0, "node-0", PodStatusRunning, true)}

	getSchedulerInstructions(pods, nodes)

	// Cordon node-0 and mark pod-0 Pending.
	nodes[0].Schedulable = false
	pods[0].markPodAsPending()
	getSchedulerInstructions(pods, nodes)

	// pod-0 is waiting within the short-term node loss window, so it
	// should be counted by CountPodsAwaitingNodeRecovery and not by stuck states.
	assert.Equal(t, tracker.CountPodsAwaitingNodeRecovery(testCacheKey), 1)
	assert.Equal(t, tracker.GetPodCountInStuckStates(testCacheKey), 0)

	// Push past the short-term node loss window. Now the pod is no longer being held
	// intentionally, so it should count as stuck and not as waiting.
	expireShortTermNodeLossWindow(t, "node-0")
	getSchedulerInstructions(pods, nodes)

	assert.Equal(t, tracker.CountPodsAwaitingNodeRecovery(testCacheKey), 0)
	assert.Equal(t, tracker.GetPodCountInStuckStates(testCacheKey), 1)
}
