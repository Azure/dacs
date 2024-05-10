package main

import (
	"fmt"
	"time"

	log "github.com/sirupsen/logrus"
	v1 "k8s.io/api/core/v1"
)

// Constant
const (
	// Duration for which pod must be in failed or unknown state to be considered stuck
	// Also duration for which pod must be in running state to consider pod healthy
	PodStableStateDurationThreshold = 5 * time.Minute

	// Duration for which pod stuck events are considered for node health
	PodStuckEventRetentionDuration = 24 * time.Hour

	// Number of recent pod stuck events to consider a node unhealthy
	SuccessivePodStuckEventThreshold = 3
)

// Information about cacheserver pod maintained by scheduler
type schedulerPodInfo struct {
	// Name of the cacheserver pod
	PodName string `json:"podName"`
	// Name of the node
	NodeName string `json:"nodeName"`
	// State of the pod
	State v1.PodPhase `json:"state"`
	// Latest timestamp this state was recorded
	LastHitTimestamp time.Time `json:"timestamp"`
	// Earliest timestamp this state was recorded
	FirstHitTimestamp time.Time `json:"firstHitTimestamp"`
}

type podStuckEvent struct {
	// Name of the cacheserver pod
	PodName string `json:"podName"`
	// State of the pod
	State v1.PodPhase `json:"state"`
	// Timestamp when the pod got stuck
	StuckTimestamp time.Time `json:"stuckTimestamp"`
}

// Information about a node maintained by scheduler
type schedulerNodeInfo struct {
	// Last known node information
	NodeInfo Node `json:"nodeInfo"`

	// Last known node information timestamp
	LastUpdatedTime time.Time `json:"lastUpdatedTime"`

	// Timestamps of cacheserver pods getting stuck on this node.
	// Cleared if a pod runs successfully on the node.
	PodsStuckEvents []podStuckEvent `json:"podsStuckEvents"`
}

func addPodStuckEvent(nodeInfo *schedulerNodeInfo, podState schedulerPodInfo) {
	// Add the pod to the stuck events list
	nodeInfo.PodsStuckEvents = append(nodeInfo.PodsStuckEvents, podStuckEvent{
		PodName:        podState.PodName,
		State:          podState.State,
		StuckTimestamp: podState.LastHitTimestamp,
	})
}

func getRecentPodStuckEventCount(nodeInfo schedulerNodeInfo) int {
	recentPodStuckEventCount := 0
	for _, podStuckEvent := range nodeInfo.PodsStuckEvents {
		if time.Since(podStuckEvent.StuckTimestamp) < PodStuckEventRetentionDuration {
			recentPodStuckEventCount++
		}
	}
	return recentPodStuckEventCount
}

// Internal state of the scheduler
type schedulerInternalState struct {
	// Map of nodes. key is the node name and value is the node information
	NodeMap map[string]schedulerNodeInfo `json:"nodeMap"`
	// Map of pods. key is the pod name and value is the pod information
	PodMap map[string]schedulerPodInfo `json:"podMap"`
}

// Collection of nodes. Key is node name, value is true
type nodeList map[string]bool

// Types of instructions
type SchedulerInstructionType string

const (
	// Bind the pod to the destination node
	BindInstruction SchedulerInstructionType = "Bind"
	// Delete the pod
	DeleteInstruction SchedulerInstructionType = "Delete"
)

// ScheduleInsturction is the instruction from scheduler
type SchedulerInstruction struct {
	// Type of instruction
	Type SchedulerInstructionType `json:"type"`
	// Name of the pod
	PodName string `json:"podName"`
	// Name of the destination node
	DestinationNode string `json:"destinationNode"`
	// Reason for the instruction
	Reason string `json:"reason"`
}

// internal variable representing scheduler state
var schedulerState schedulerInternalState = schedulerInternalState{
	NodeMap: map[string]schedulerNodeInfo{},
	PodMap:  map[string]schedulerPodInfo{},
}

// helper to add node to nodeList
func (nl nodeList) add(nodeName string) nodeList {
	nl[nodeName] = true
	return nl
}

// helper to remove node from nodeList
func (nl nodeList) remove(nodeName string) nodeList {
	delete(nl, nodeName)
	return nl
}

// helper to get get first element node from nodeList.
// Note: nodeList is a map which is unordered
func (nl nodeList) getFirstElement() string {
	// return first element in the map (maps are unordered)
	for nodeName := range nl {
		return nodeName
	}
	return ""
}

func contains(arr []string, val string) bool {
	for _, v := range arr {
		if v == val {
			return true
		}
	}
	return false
}

// helper function to update scheduler nodeMap
func updateNodeMap(nodes []Node) (schedulableNodes nodeList) {
	schedulableNodes = nodeList{}
	for _, node := range nodes {
		nodeInfo, nodeInfoFoundOk := schedulerState.NodeMap[node.NodeName]
		if !nodeInfoFoundOk {
			nodeInfo = schedulerNodeInfo{
				NodeInfo:        node,
				LastUpdatedTime: time.Now(),
				PodsStuckEvents: []podStuckEvent{},
			}
		} else {
			nodeInfo.NodeInfo = node
			nodeInfo.LastUpdatedTime = time.Now()
		}

		if node.Schedulable {
			schedulableNodes.add(node.NodeName)
		} else {
			log.Debugf("Node %s is not schedulable", node.NodeName)
		}

		schedulerState.NodeMap[node.NodeName] = nodeInfo
		log.WithFields(log.Fields{
			"nodeName":           node.NodeName,
			"schedulable":        node.Schedulable,
			"processingUnit":     node.ProcessingUnit,
			"agentpool":          node.Agentpool,
			"lastUpdated":        nodeInfo.LastUpdatedTime,
			"podStuckEventsSize": len(nodeInfo.PodsStuckEvents),
		}).Debugf("Scheduler processed node %s", node.NodeName)
	}

	return schedulableNodes
}

// helper function to update scheduler podMap
func updatePodMap(pods []Pod) (pendingPods []Pod, stuckPods []Pod, nodesWithPods nodeList) {
	pendingPods = []Pod{}
	stuckPods = []Pod{}
	nodesWithPod := nodeList{}
	for _, pod := range pods {
		if pod.NodeName != "" {
			nodesWithPod.add(pod.NodeName)

			podState, existingPodStateFound := schedulerState.PodMap[pod.PodName]
			if existingPodStateFound && podState.NodeName == pod.NodeName && podState.State == pod.State {
				podState.LastHitTimestamp = time.Now()
			} else {
				podState = schedulerPodInfo{
					PodName:           pod.PodName,
					NodeName:          pod.NodeName,
					State:             pod.State,
					LastHitTimestamp:  time.Now(),
					FirstHitTimestamp: time.Now(),
				}
			}

			schedulerState.PodMap[pod.PodName] = podState
			log.WithFields(log.Fields{
				"podName":           pod.PodName,
				"nodeName":          pod.NodeName,
				"state":             pod.State,
				"lastHitTimestamp":  podState.LastHitTimestamp,
				"firstHitTimestamp": podState.FirstHitTimestamp,
			}).Debugf("Scheduler processed pod %s", pod.PodName)

			// Conside pod as stuck if it has been in failed or unknown state for more than PodStableStateDurationThreshold
			isPodStuck := (pod.State == v1.PodFailed || pod.State == v1.PodUnknown) &&
				(time.Since(podState.FirstHitTimestamp) > PodStableStateDurationThreshold)
			if isPodStuck {
				log.Warnf("Pod %s is stuck on node %s in %s state", pod.PodName, pod.NodeName, pod.State)

				stuckNodeInfo, stuckNodeFound := schedulerState.NodeMap[pod.NodeName]
				if stuckNodeFound {
					addPodStuckEvent(&stuckNodeInfo, podState)
					schedulerState.NodeMap[pod.NodeName] = stuckNodeInfo
				}

				stuckPods = append(stuckPods, pod)
			}

			// If pod is running for more than PodStableStateDurationThreshold, clear the node's stuck events
			if pod.State == v1.PodRunning && (time.Since(podState.FirstHitTimestamp) > PodStableStateDurationThreshold) {
				nodeInfo, nodeInfoFound := schedulerState.NodeMap[pod.NodeName]
				if nodeInfoFound && len(nodeInfo.PodsStuckEvents) > 0 {
					log.Infof("Pod %s is running on node %s. Clearing node's stuck events", pod.PodName, pod.NodeName)
					nodeInfo.PodsStuckEvents = []podStuckEvent{}
					schedulerState.NodeMap[pod.NodeName] = nodeInfo
				}
			}
		} else {
			log.Infof("Pod %s is pending", pod.PodName)
			pendingPods = append(pendingPods, pod)
		}
	}

	return pendingPods, stuckPods, nodesWithPod
}

// helper function to choose a node to schedule a pod
func findNodeToSchedule(podToSchedule Pod, schedulableNodesWithoutPod nodeList) (targetNode string, reason string) {
	log.WithFields(log.Fields{
		"podName":                podToSchedule.PodName,
		"state":                  podToSchedule.State,
		"allowedProcessingUnits": podToSchedule.AllowedProcessingUnits,
		"allowedAgentpools":      podToSchedule.AllowedAgentpools,
	}).Infof("Looking for a node to schedule pod %s", podToSchedule.PodName)

	// Build list of schedulable free nodes matching pod affinity
	schedulableFreeNodesMatchingAffinity := nodeList{}
	schedulableFreeNodesMatchingAffinityWithoutPodError := nodeList{}
	for nodeName := range schedulableNodesWithoutPod {
		nodeInfo := schedulerState.NodeMap[nodeName].NodeInfo

		if len(podToSchedule.AllowedProcessingUnits) > 0 {
			if nodeInfo.ProcessingUnit == "" || !contains(podToSchedule.AllowedProcessingUnits, nodeInfo.ProcessingUnit) {
				log.Infof("Node %s does not match processing unit node affinity for pod %s. AllowedProcessingUnits=%v NodeProcessingUnit=%s",
					nodeName, podToSchedule.PodName, podToSchedule.AllowedProcessingUnits, nodeInfo.ProcessingUnit)
				continue
			}
		}

		if len(podToSchedule.AllowedAgentpools) > 0 {
			if nodeInfo.Agentpool == "" || !contains(podToSchedule.AllowedAgentpools, nodeInfo.Agentpool) {
				log.Infof("Node %s does not match agentpool node affinity for pod %s. AllowedAgentpools=%v NodeAgentpool=%s",
					nodeName, podToSchedule.PodName, podToSchedule.AllowedAgentpools, nodeInfo.Agentpool)
				continue
			}
		}

		nodeRecentErrors := getRecentPodStuckEventCount(schedulerState.NodeMap[nodeName])
		schedulableFreeNodesMatchingAffinity.add(nodeName)
		if nodeRecentErrors < SuccessivePodStuckEventThreshold {
			schedulableFreeNodesMatchingAffinityWithoutPodError.add(nodeName)
		}
	}

	// Lookup the last known node running pod in podMap
	lastNodeRunningPod, lastNodeRunningPodFound := schedulerState.PodMap[podToSchedule.PodName]
	if lastNodeRunningPodFound {
		// If the pod was running on a node, try to schedule it on the same node if it matches constraints and has no errors
		_, lastNodeRunningPodHasNoError := schedulableFreeNodesMatchingAffinityWithoutPodError[lastNodeRunningPod.NodeName]
		if lastNodeRunningPodHasNoError {
			log.Infof("Pod %s was previously running on node %s, scheduling on the same node", podToSchedule.PodName, lastNodeRunningPod.NodeName)
			return lastNodeRunningPod.NodeName, "Rescheduling pod on the node it was previously running on"
		} else {
			log.Infof("Pod %s was previously running on node %s, but node is no longer free, schedulable and without recent cacheserver errors",
				podToSchedule.PodName, lastNodeRunningPod.NodeName)
		}
	} else {
		log.Infof("Could not find last known node running pod %s", podToSchedule.PodName)
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
		"No eligible node found to schedule pod %s. SchedulableFreeNodesMatchingAffinity=%d SchedulableFreeNodesMatchingAffinityWithoutErrors=%d",
		podToSchedule.PodName, len(schedulableFreeNodesMatchingAffinity), len(schedulableFreeNodesMatchingAffinityWithoutPodError))
	log.Errorln(errorStr)
	return "", errorStr
}

// SchedulerPeriodicStep processes the cacheserver nodes and pods and returns the ScheduleInstruction
// This function is supposed to be called periodically by main function
func SchedulerPeriodicStep(pods []Pod, nodes []Node) []SchedulerInstruction {
	log.Infof("Scheduler started processing %d nodes and %d pods", len(nodes), len(pods))

	// Update the nodeMap
	schedulableNodes := updateNodeMap(nodes)

	// Update the nodeMap
	pendingPods, stuckPods, nodesWithPod := updatePodMap(pods)
	log.Infof("Found %d pending pods, %d stuck pods", len(pendingPods), len(stuckPods))

	// Issue delete instructions for stuck pods
	schedulerInstructions := []SchedulerInstruction{}
	for _, stuckPod := range stuckPods {
		instructionReason := fmt.Sprintf("Pod %s is stuck on node %s in %s state for more than %s",
			stuckPod.PodName, stuckPod.NodeName, stuckPod.State, PodStableStateDurationThreshold)
		log.Infof("Issuing delete instruction for pod %s. Reason: %s", stuckPod.PodName, instructionReason)
		schedulerInstructions = append(schedulerInstructions, SchedulerInstruction{
			Type:            DeleteInstruction,
			PodName:         stuckPod.PodName,
			DestinationNode: "",
			Reason:          instructionReason,
		})
	}

	// Compute list of nodes without cacheserver pods
	schedulableNodesWithoutPod := nodeList{}
	for node := range schedulableNodes {
		if _, found := nodesWithPod[node]; !found {
			schedulableNodesWithoutPod.add(node)
		}
	}
	log.Debugf("Found %d nodes without cacheserver pods. TotalNodes=%d NodesWithPods=%d",
		len(schedulableNodesWithoutPod), len(nodes), len(nodesWithPod))

	// For each pod to schedule, find a node to schedule it on
	for _, pendingPod := range pendingPods {
		if pendingPod.SchedulerBypassed {
			log.Infof("Pod %s is bypassing scheduler", pendingPod.PodName)
			continue
		}

		// Find a node to schedule the pod
		targetNode, reason := findNodeToSchedule(pendingPod, schedulableNodesWithoutPod)
		if targetNode != "" {
			schedulableNodesWithoutPod.remove(targetNode) // remove so that next pending pod in loop doesn't schedule on same node
		} else {
			// Expand reason string with diagnostic information in case no node was found to schedule
			reason = fmt.Sprintf("%s TotalNodes=%d SchedulableNodes=%d SchedulableNodesWithoutPod=%d NodesWithPod=%d",
				reason, len(nodes), len(schedulableNodes), len(schedulableNodesWithoutPod), len(nodesWithPod))
		}

		schedulerInstructions = append(schedulerInstructions, SchedulerInstruction{
			Type:            BindInstruction,
			PodName:         pendingPod.PodName,
			DestinationNode: targetNode,
			Reason:          reason,
		})
	}

	return schedulerInstructions
}
