package management

import (
	"context"
	"fmt"
	"sort"
	"strconv"
	"strings"

	log "github.com/sirupsen/logrus"
	v1 "k8s.io/api/core/v1"
	ctrlclient "sigs.k8s.io/controller-runtime/pkg/client"
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
func findNodeToSchedule(tracker Tracker, podToSchedule Pod, occupiedNodes nodeList) (targetNode string, reason string) {
	log.WithFields(log.Fields{
		"namespace":         podToSchedule.Namespace,
		"podName":           podToSchedule.PodName,
		"state":             podToSchedule.State,
		"nodeSelectorKey":   podToSchedule.NodeSelectorKey,
		"nodeSelectorValue": podToSchedule.NodeSelectorValue,
		"requests":          formatResourceList(podToSchedule.Requests),
	}).Infof("Looking for a node to schedule pod %s", podToSchedule.PodName)

	// Build list of schedulable free nodes matching pod affinity
	totalNodes := tracker.GetTotalNodeCount()
	nodesMatchingAffinity := tracker.GetEligibleNodesMatchingAffinity(podToSchedule.NodeSelectorKey, podToSchedule.NodeSelectorValue)
	schedulableFreeNodesMatchingAffinity := nodeList{}
	schedulableFreeNodesMatchingAffinityWithoutPodError := nodeList{}
	for _, node := range nodesMatchingAffinity {
		isOccupied := occupiedNodes[node.NodeName]
		if !isOccupied {
			if !hasSufficientResources(&node, &podToSchedule) {
				log.Infof("Node %s does not have sufficient resources for pod %s. NodeAllocatable=%s PodRequests=%s",
					node.NodeName, podToSchedule.PodName, formatResourceList(node.Allocatable), formatResourceList(podToSchedule.Requests))
				continue
			}
			schedulableFreeNodesMatchingAffinity.add(node.NodeName)
			if !tracker.DoesNodeHavePodErrors(node.NodeName) {
				schedulableFreeNodesMatchingAffinityWithoutPodError.add(node.NodeName)
			}
		}
	}

	// Lookup the last known node running pod in podMap
	lastNodeRunningPod, lastNodeRunningPodFound := tracker.GetLastRunningNodeForPod(podToSchedule.Namespace, podToSchedule.PodName)
	if lastNodeRunningPodFound {
		// If the pod was running on a node, try to schedule it on the same node if it matches constraints and has no errors
		_, lastNodeRunningPodHasNoError := schedulableFreeNodesMatchingAffinityWithoutPodError[lastNodeRunningPod]
		if lastNodeRunningPodHasNoError {
			log.Infof("Pod %s(%s) was previously running on node %s, scheduling on the same node",
				podToSchedule.PodName, podToSchedule.Namespace, lastNodeRunningPod)
			return lastNodeRunningPod, "Rescheduling pod on the node it was previously running on"
		} else {
			log.Infof("Pod %s(%s) was previously running on node %s, "+
				"but node is no longer free, schedulable, has sufficient resources, and without recent cacheserver errors",
				podToSchedule.PodName, podToSchedule.Namespace, lastNodeRunningPod)
		}
	} else {
		log.Infof("Could not find last known node running pod %s(%s)", podToSchedule.PodName, podToSchedule.Namespace)
	}

	// Schedule it on a node with no errors, if such node is available
	if len(schedulableFreeNodesMatchingAffinityWithoutPodError) > 0 {
		targetNode := schedulableFreeNodesMatchingAffinityWithoutPodError.getFirstElement()

		log.Infof("Scheduling pod %s on healthy node %s", podToSchedule.PodName, targetNode)
		return targetNode, "Scheduling pod on healthy node"
	}

	// No node without recent cacheserver errors available, try to schedule on a node with errors
	if len(schedulableFreeNodesMatchingAffinity) > 0 {
		targetNode := schedulableFreeNodesMatchingAffinity.getFirstElement()

		log.Infof("Scheduling pod %s on node %s which has recent cacheserver errors", podToSchedule.PodName, targetNode)
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
	log.Errorln(errorStr)
	return "", errorStr
}

// Schedule() processes the cacheserver nodes and pods and returns the ScheduleInstruction
// This function is supposed to be called on every change in pods or nodes
//
// This custom scheduling logic tries to schedule pods on nodes they were previously running on,
// in order to not lose the cached data on those nodes. If that is not possible, it tries to schedule
// pods on healthy nodes (nodes without recent cacheserver errors). If no healthy nodes are available,
// it falls back to scheduling on any available node matching the pod's node affinity.
func Schedule(ctx context.Context, tracker Tracker, k8sClient ctrlclient.Client) []SchedulerInstruction {
	nodeCount := tracker.GetTotalNodeCount()
	occupiedNodes := tracker.GetNodesWithPods()
	log.Debugf("Found %d total nodes, %d nodes with pods", nodeCount, len(occupiedNodes))

	scheduledPods, pendingPods := tracker.GetCurrentPods()
	stuckPods := tracker.GetStuckPods()
	log.Debugf("Found %d pending pods, %d scheduled pods, %d stuck pods", len(pendingPods), len(scheduledPods), len(stuckPods))

	// Issue delete instructions for stuck pods.
	schedulerInstructions := []SchedulerInstruction{}
	for _, stuckPod := range stuckPods {
		instructionReason := fmt.Sprintf("Pod %s(%s) is stuck on node %s in %s state for more than %s",
			stuckPod.PodName, stuckPod.Namespace, stuckPod.NodeName, stuckPod.State, tracker.Config.PodStuckThresholdDuration)
		log.Infof("Issuing delete instruction for pod %s(%s). Reason: %s", stuckPod.PodName, stuckPod.Namespace, instructionReason)
		schedulerInstructions = append(schedulerInstructions, SchedulerInstruction{
			Type:            DeleteInstruction,
			PodName:         stuckPod.PodName,
			Namespace:       stuckPod.Namespace,
			DestinationNode: "",
			Reason:          instructionReason,
		})
	}

	// Issue delete instructions for excess pods (index >= desired replicas)
	allPods := append(scheduledPods, pendingPods...)
	excessPods := identifyExcessPods(ctx, k8sClient, allPods)
	for _, excessPod := range excessPods {
		instructionReason := fmt.Sprintf("Pod %s(%s) has index >= desired replica count, cleaning up excess pod",
			excessPod.PodName, excessPod.Namespace)
		log.Infof("Issuing delete instruction for excess pod %s(%s). Reason: %s", excessPod.PodName, excessPod.Namespace, instructionReason)
		schedulerInstructions = append(schedulerInstructions, SchedulerInstruction{
			Type:            DeleteInstruction,
			PodName:         excessPod.PodName,
			Namespace:       excessPod.Namespace,
			DestinationNode: "",
			Reason:          instructionReason,
		})
	}

	// For each pod to schedule, find a node to schedule it on
	for _, pendingPod := range pendingPods {
		if pendingPod.SchedulerBypassed {
			log.Infof("Pod %s(%s) is bypassing scheduler", pendingPod.PodName, pendingPod.Namespace)
			continue
		}

		// Find a node to schedule the pod
		targetNode, reason := findNodeToSchedule(tracker, pendingPod, occupiedNodes)
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

// identifyExcessPods identifies pods with index >= desired replica count
func identifyExcessPods(ctx context.Context, k8sClient ctrlclient.Client, allPods []Pod) []Pod {
	desiredReplicasMap := make(map[string]int)
	failedNamespaces := make(map[string]bool)

	// Build desired replicas map
	for _, pod := range allPods {
		namespace := pod.Namespace
		if failedNamespaces[namespace] {
			continue
		}
		if _, exists := desiredReplicasMap[namespace]; !exists {
			// Note: we are directly issuing k8s API call in the middle of scheduling logic here, which is not ideal.
			// In the future, we can have the tracker track the cache StatefulSet and query it instead (like we do for nodes and pods)
			desiredReplicas, err := GetDesiredReplicaCountFromK8s(ctx, k8sClient, namespace)
			if err != nil {
				failedNamespaces[namespace] = true
				log.Warnf("Unable to get desired replica count for namespace %s: %v."+
					"Skipping excess pod cleanup for this namespace", namespace, err)
				continue
			}
			desiredReplicasMap[namespace] = desiredReplicas
		}
	}

	excessPods := []Pod{}
	for _, pod := range allPods {
		if !failedNamespaces[pod.Namespace] {
			index := extractPodIndexFromName(pod.PodName)
			desiredReplicas := desiredReplicasMap[pod.Namespace]
			if index >= 0 && index >= desiredReplicas {
				log.Infof("Identified excess pod %s (index=%d, desired=%d, state=%s)",
					pod.PodName, index, desiredReplicas, pod.State)
				excessPods = append(excessPods, pod)
			}
		}
	}
	return excessPods
}

// extractPodIndexFromName extracts numeric index from StatefulSet pod name
// Example: "cacheserver-5" -> 5, "cacheserver-22" -> 22
func extractPodIndexFromName(podName string) int {
	lastDashIndex := strings.LastIndex(podName, "-")
	if lastDashIndex == -1 {
		return -1
	}

	indexStr := podName[lastDashIndex+1:]
	index, err := strconv.Atoi(indexStr)
	if err != nil {
		return -1
	}

	return index
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
