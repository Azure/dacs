package management

import (
	"context"
	"fmt"
	"strings"
	"testing"
	"time"

	"gotest.tools/v3/assert"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/resource"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	clientgoscheme "k8s.io/client-go/kubernetes/scheme"
	ctrlclient "sigs.k8s.io/controller-runtime/pkg/client"
	ctrlfake "sigs.k8s.io/controller-runtime/pkg/client/fake"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	config "github.com/Azure/dacs/cache-controller/internal/config"
)

var k8sClient ctrlclient.Client
var tracker Tracker

func newTestScheme() *runtime.Scheme {
	s := runtime.NewScheme()
	_ = clientgoscheme.AddToScheme(s)
	_ = storagev1.AddToScheme(s)
	return s
}

func init() {
	k8sClient = ctrlfake.NewClientBuilder().WithScheme(newTestScheme()).Build()
	mockLazyInit(k8sClient)
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
	config := &config.Config{
		PodStuckThresholdDuration: 5 * time.Minute,
	}
	newTracker := InitTracker(config)
	tracker = newTracker
}

// mockLazyInit sets the k8sClient to the provided fake client.
func mockLazyInit(fakeClient ctrlclient.Client) {
	k8sClient = fakeClient
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

func getSchedulerInstructions(pods []Pod, nodes []Node) []SchedulerInstruction {
	// Update tracker
	tracker.UpdateTracker(nodes, pods, []storagev1.Cache{})

	// Get scheduler instructions
	return Schedule(context.TODO(), tracker, k8sClient)
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

func TestPodMovesWhenNodeRemovedOrCordoned(t *testing.T) {
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

	// Mark node-3 as cordoned and pod-1 as pending, and reprocess. Pod-1 should get scheduled on "node-1"
	nodes[3].Schedulable = false
	pods[1].markPodAsPending()
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

func TestNodeCordon(t *testing.T) {
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

	// Mark node-1 as cordoned and pod-1 as pending, and reprocess.
	// Pod-1 should get scheduled on "node-3", different from "node-1" it was running on previously
	nodes[1].Schedulable = false
	pods[1].markPodAsPending()
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

// TestExcessPodsCleanup tests the scenario
// where 10 pods were running (cacheserver-0,1,2,3,4,6,8,9,16,22)
// but only 6 were desired.
// This caused a deadlock where cacheserver-5 was pending and couldn't be scheduled.
// Also tests that excess pods are deleted regardless of their state.
func TestExcessPodsCleanup(t *testing.T) {
	resetTracker()

	// Create a mock StatefulSet with 6 desired replicas
	statefulset := createMockStatefulSet("cacheserver", "azureml", 6)
	k8sClient = ctrlfake.NewClientBuilder().WithScheme(newTestScheme()).WithObjects(statefulset).Build()
	mockLazyInit(k8sClient)

	// Create 10 nodes (enough for the desired 6 pods)
	nodes := CreateMockGpuNodeList(10)

	// Simulate the real-world scenario: 10 pods with various states but only 6 desired
	// Running pods: cacheserver-0, 1, 2, 3, 4 (desired, all running)
	// Excess pods in different states:
	// cacheserver-6 (Running), 8 (Terminating), 9 (NotReady), 16 (Failed), 22 (Running)
	pods := []Pod{}
	pods = append(pods, CreateMockPod("azureml", 0, "node-0", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("azureml", 1, "node-1", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("azureml", 2, "node-2", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("azureml", 3, "node-3", PodStatusRunning, true))
	pods = append(pods, CreateMockPod("azureml", 4, "node-4", PodStatusRunning, true))
	// Excess pods in various states
	pod6 := CreateMockPod("azureml", 6, "node-5", PodStatusRunning, true)
	pod6.PodName = "cacheserver-6"
	pods = append(pods, pod6)

	pod8 := CreateMockPod("azureml", 8, "node-6", PodStatusTerminating, true)
	pod8.PodName = "cacheserver-8"
	pods = append(pods, pod8)

	pod9 := CreateMockPod("azureml", 9, "node-7", PodStatusNotReady, true)
	pod9.PodName = "cacheserver-9"
	pods = append(pods, pod9)

	pod16 := CreateMockPod("azureml", 16, "node-8", PodStatusFailed, true)
	pod16.PodName = "cacheserver-16"
	pods = append(pods, pod16)

	pod22 := CreateMockPod("azureml", 22, "node-9", PodStatusRunning, true)
	pod22.PodName = "cacheserver-22"
	pods = append(pods, pod22)

	// cacheserver-5 is pending (can't find a free node)
	pendingPod := CreateMockPod("azureml", 5, "", PodStatusPending, true)
	pendingPod.PodName = "cacheserver-5"
	pods = append(pods, pendingPod)

	// Run scheduler - should identify excess pods and issue delete instructions
	schedulerInstructions := getSchedulerInstructions(pods, nodes)

	// Verify the correct pods are being deleted with proper reason
	assertPodDeleted(t, schedulerInstructions, "azureml", "cacheserver-6",
		"Pod cacheserver-6(azureml) has index >= desired replica count, cleaning up excess pod")
	assertPodDeleted(t, schedulerInstructions, "azureml", "cacheserver-8",
		"Pod cacheserver-8(azureml) has index >= desired replica count, cleaning up excess pod")
	assertPodDeleted(t, schedulerInstructions, "azureml", "cacheserver-9",
		"Pod cacheserver-9(azureml) has index >= desired replica count, cleaning up excess pod")
	assertPodDeleted(t, schedulerInstructions, "azureml", "cacheserver-16",
		"Pod cacheserver-16(azureml) has index >= desired replica count, cleaning up excess pod")
	assertPodDeleted(t, schedulerInstructions, "azureml", "cacheserver-22",
		"Pod cacheserver-22(azureml) has index >= desired replica count, cleaning up excess pod")
	// Verify cacheserver-5 is pending and cannot be scheduled due to no available nodes
	pod5Instruction := getSchedulerInstructionForPod(schedulerInstructions, "azureml", "cacheserver-5")
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
	schedulerInstructions = getSchedulerInstructions(podsAfterDeletion, nodes)
	freeNodes := nodeList{"node-5": true, "node-6": true, "node-7": true, "node-8": true, "node-9": true}
	assertPodScheduledToOneOfNodes(t, schedulerInstructions, "azureml", "cacheserver-5", freeNodes, "Scheduling pod on healthy node")
}

// TestNoExcessPodsWhenDesiredMatches tests that no pods are deleted when
// the desired replica count matches the running pod count and indices
func TestNoExcessPodsWhenDesiredMatches(t *testing.T) {
	resetTracker()

	// Create a mock StatefulSet with 4 desired replicas
	statefulset := createMockStatefulSet("cacheserver", "azureml", 4)
	k8sClient = ctrlfake.NewClientBuilder().WithScheme(newTestScheme()).WithObjects(statefulset).Build()
	mockLazyInit(k8sClient)

	nodes := CreateMockGpuNodeList(4)

	// Running pods: cacheserver-0, 1, 2, 3 (desired configuration)
	pods := []Pod{}
	for i := 0; i < 4; i++ {
		pod := CreateMockPod("azureml", i, fmt.Sprintf("node-%d", i), PodStatusRunning, true)
		pods = append(pods, pod)
	}

	// Run scheduler
	schedulerInstructions := getSchedulerInstructions(pods, nodes)

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
	resetTracker()

	// Create a mock StatefulSet
	statefulset := createMockStatefulSet("cacheserver", "azureml", 3)
	k8sClient = ctrlfake.NewClientBuilder().WithScheme(newTestScheme()).WithObjects(statefulset).Build()
	mockLazyInit(k8sClient)

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

	// Create a mock StatefulSet
	statefulset := createMockStatefulSet("cacheserver", "azureml", 2)
	k8sClient = ctrlfake.NewClientBuilder().WithScheme(newTestScheme()).WithObjects(statefulset).Build()
	mockLazyInit(k8sClient)

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
