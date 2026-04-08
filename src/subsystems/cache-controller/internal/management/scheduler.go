package management

import (
	"context"
	"fmt"
	"sort"
	"strings"

	v1 "k8s.io/api/core/v1"
	logf "sigs.k8s.io/controller-runtime/pkg/log"
)

// Types of instructions
type SchedulerInstructionType string

const (
	// Bind the pod to the destination node
	BindInstruction SchedulerInstructionType = "Bind"
	// Delete the pod
	DeleteInstruction SchedulerInstructionType = "Delete"
)

// SchedulerInstruction is the instruction from scheduler
type SchedulerInstruction struct {
	// Type of instruction
	Type SchedulerInstructionType `json:"type"`
	// Name of the pod
	PodName string `json:"podName"`
	// Namespace of the pod
	Namespace string `json:"namespace"`
	// Name of the destination node
	DestinationNode string `json:"destinationNode"`
	// Reason for the instruction
	Reason string `json:"reason"`
}

// helper function to choose a node to schedule a pod
func findNodeToSchedule(ctx context.Context, tracker *Tracker, podToSchedule Pod, occupiedNodes nodeList) (targetNode string, reason string) {
	log := logf.FromContext(ctx)
	log.Info("Looking for a node to schedule pod",
		"namespace", podToSchedule.Namespace,
		"podName", podToSchedule.PodName,
		"state", podToSchedule.State,
		"nodeSelectorKey", podToSchedule.NodeSelectorKey,
		"nodeSelectorValue", podToSchedule.NodeSelectorValue)

	// Build list of schedulable free nodes matching pod affinity
	totalNodes := tracker.getTotalNodeCount()
	nodesMatchingAffinity := tracker.getEligibleNodesMatchingAffinity(podToSchedule.NodeSelectorKey, podToSchedule.NodeSelectorValue)
	schedulableFreeNodesMatchingAffinity := nodeList{}
	schedulableFreeNodesMatchingAffinityWithoutPodError := nodeList{}
	for _, node := range nodesMatchingAffinity {
		isOccupied := occupiedNodes[node.NodeName]
		if !isOccupied {
			if !hasSufficientResources(&node, &podToSchedule) {
				log.Info("Node does not have sufficient resources for pod",
					"node", node.NodeName, "pod", podToSchedule.PodName,
					"nodeAllocatable", formatResourceList(node.Allocatable),
					"podRequests", formatResourceList(podToSchedule.Requests))
				continue
			}
			schedulableFreeNodesMatchingAffinity.add(node.NodeName)
			if !tracker.doesNodeHavePodErrors(node.NodeName) {
				schedulableFreeNodesMatchingAffinityWithoutPodError.add(node.NodeName)
			}
		}
	}

	// Lookup the last known node running pod in podMap
	lastNodeRunningPod, lastNodeRunningPodFound := tracker.getLastRunningNodeForPod(podToSchedule.Namespace, podToSchedule.PodName)
	if lastNodeRunningPodFound {
		// If the pod was running on a node, try to schedule it on the same node if it matches constraints and has no errors
		_, lastNodeRunningPodHasNoError := schedulableFreeNodesMatchingAffinityWithoutPodError[lastNodeRunningPod]
		if lastNodeRunningPodHasNoError {
			log.Info("Pod was previously running on node, scheduling on the same node",
				"podName", podToSchedule.PodName, "namespace", podToSchedule.Namespace, "node", lastNodeRunningPod)
			return lastNodeRunningPod, "Rescheduling pod on the node it was previously running on"
		} else {
			log.Info("Pod was previously running on node, but node is no longer free, schedulable and without recent cacheserver errors",
				"podName", podToSchedule.PodName, "namespace", podToSchedule.Namespace, "node", lastNodeRunningPod)
		}
	} else {
		log.Info("Could not find last known node running pod",
			"podName", podToSchedule.PodName, "namespace", podToSchedule.Namespace)
	}

	// Schedule it on a node with no errors, if such node is available
	if len(schedulableFreeNodesMatchingAffinityWithoutPodError) > 0 {
		targetNode := schedulableFreeNodesMatchingAffinityWithoutPodError.getFirstElement()

		log.Info("Scheduling pod on healthy node", "podName", podToSchedule.PodName, "node", targetNode)
		return targetNode, "Scheduling pod on healthy node"
	}

	// No node without recent cacheserver errors available, try to schedule on a node with errors
	if len(schedulableFreeNodesMatchingAffinity) > 0 {
		targetNode := schedulableFreeNodesMatchingAffinity.getFirstElement()

		log.Info("Scheduling pod on node which has recent cacheserver errors", "podName", podToSchedule.PodName, "node", targetNode)
		return targetNode, "Scheduling pod on healthy node (warning: node has recent cacheserver errors)"
	}

	// If no eligible node found, return an empty destination node (indicating error) along with reason
	var errorStr = fmt.Sprintf(
		"No eligible node found to schedule pod %s. totalNodes=%d, "+
			"schedulableNodesMatchingAffinity=%d, schedulableFreeNodesMatchingAffinity=%d, "+
			"occupiedNodes=%d",
		podToSchedule.PodName, totalNodes,
		len(nodesMatchingAffinity), len(schedulableFreeNodesMatchingAffinity),
		len(occupiedNodes))
	log.Info(errorStr)
	return "", errorStr
}

// Schedule() processes the cacheserver nodes and pods and returns the ScheduleInstruction
// This function is supposed to be called on every change in pods or nodes
//
// This custom scheduling logic tries to schedule pods on nodes they were previously running on,
// in order to not lose the cached data on those nodes. If that is not possible, it tries to schedule
// pods on healthy nodes (nodes without recent cacheserver errors). If no healthy nodes are available,
// it falls back to scheduling on any available node matching the pod's node affinity.
func schedule(ctx context.Context, tracker *Tracker, cacheKey string) []SchedulerInstruction {
	log := logf.FromContext(ctx)
	nodeCount := tracker.getTotalNodeCount()
	occupiedNodes := tracker.getNodesWithPods()
	log.V(1).Info("Found nodes", "totalNodes", nodeCount, "nodesWithPods", len(occupiedNodes))

	scheduledPods, pendingPods := tracker.getCurrentPods(cacheKey)
	stuckPods := tracker.getStuckPods(cacheKey)
	log.V(1).Info("Found pods", "pendingPods", len(pendingPods), "scheduledPods", len(scheduledPods), "stuckPods", len(stuckPods))

	schedulerInstructions := []SchedulerInstruction{}

	// Issue delete instructions for stuck pods.
	for _, stuckPod := range stuckPods {
		instructionReason := fmt.Sprintf("Pod %s(%s) is stuck on node %s in %s state for more than %s",
			stuckPod.PodName, stuckPod.Namespace, stuckPod.NodeName, stuckPod.State, tracker.Config.PodStuckThresholdDuration)
		log.Info("Issuing delete instruction for stuck pod",
			"podName", stuckPod.PodName, "namespace", stuckPod.Namespace, "reason", instructionReason)
		schedulerInstructions = append(schedulerInstructions, SchedulerInstruction{
			Type:            DeleteInstruction,
			PodName:         stuckPod.PodName,
			Namespace:       stuckPod.Namespace,
			DestinationNode: "",
			Reason:          instructionReason,
		})
	}

	// Excess pods (ordinal >= desired replicas) are NOT deleted here.
	// The StatefulSet controller handles scale-down gracefully, honoring
	// terminationGracePeriodSeconds and PreStop hooks. Force-deleting
	// excess pods would bypass that graceful shutdown.

	// For each pod to schedule, find a node to schedule it on
	for _, pendingPod := range pendingPods {
		if pendingPod.SchedulerBypassed {
			log.Info("Pod is bypassing scheduler", "podName", pendingPod.PodName, "namespace", pendingPod.Namespace)
			continue
		}

		// Find a node to schedule the pod
		targetNode, reason := findNodeToSchedule(ctx, tracker, pendingPod, occupiedNodes)
		if targetNode != "" {
			occupiedNodes.add(targetNode)
		}

		schedulerInstructions = append(schedulerInstructions, SchedulerInstruction{
			Type:            BindInstruction,
			PodName:         pendingPod.PodName,
			Namespace:       pendingPod.Namespace,
			DestinationNode: targetNode,
			Reason:          reason,
		})
	}

	return schedulerInstructions
}

// Helper function to check if node has sufficient resources for the pod
func hasSufficientResources(nodeInfo *Node, pod *Pod) bool {
	for requestedResourceName, requestedResourceQuantity := range pod.Requests {
		if requestedResourceQuantity.IsZero() {
			continue
		}
		allocatableQuantity, found := nodeInfo.Allocatable[requestedResourceName]
		if !found || allocatableQuantity.Cmp(requestedResourceQuantity) < 0 {
			return false
		}
	}
	return true
}

// Helper function to format resource list for logging
func formatResourceList(resourceList v1.ResourceList) string {
	parts := []string{}
	for k, v := range resourceList {
		parts = append(parts, fmt.Sprintf("%s=%s", k, v.String()))
	}
	sort.Strings(parts)
	return fmt.Sprintf("[%s]", strings.Join(parts, ", "))
}

// UpdateAndSchedule atomically updates the tracker with the given nodes
// and pods, then runs the scheduler to produce binding/deletion instructions.
// The Tracker mutex is held for the duration of the update-and-schedule
// sequence so concurrent reconciles see consistent global state.
func (t *Tracker) UpdateAndSchedule(ctx context.Context, cacheKey string, nodes []Node, pods []Pod) []SchedulerInstruction {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.updateTracker(ctx, cacheKey, nodes, pods)
	return schedule(ctx, t, cacheKey)
}
