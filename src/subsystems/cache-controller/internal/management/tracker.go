/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package management

import (
	"context"
	"sync"
	"time"

	config "github.com/Azure/dacs/cache-controller/internal/config"
	log "github.com/sirupsen/logrus"
)

// Constant
const (
	// Duration for which pod stuck events are considered for node health
	PodStuckEventRetentionDuration = 24 * time.Hour

	// Number of recent pod stuck events to consider a node unhealthy
	SuccessivePodStuckEventThreshold = 3
)

// List of states in which a pod can be considered stuck
var podStuckStates = []PodStatus{PodStatusPending, PodStatusFailed, PodStatusUnknown, PodStatusTerminating, PodStatusNotReady}

// Tracker configuration
type TrackerConfig struct {
	PodStuckThresholdDuration          time.Duration
	ShortTermNodeLossThresholdDuration time.Duration
}

// Information about cacheserver pod maintained by tracker
type trackerPodInfo struct {
	// Name of the cacheserver pod
	PodName string `json:"podName"`
	// Namespace of the cacheserver pod
	Namespace string `json:"namespace"`
	// Name of the node
	NodeName string `json:"nodeName"`
	// State of the pod
	State PodStatus `json:"state"`
	// Latest timestamp this state was recorded
	LastHitTimestamp time.Time `json:"timestamp"`
	// Earliest timestamp this state was recorded
	FirstHitTimestamp time.Time `json:"firstHitTimestamp"`
}

// Collection of cacheserver pods. Key is pod name, value is pod information
type trackerPodCollection map[string]trackerPodInfo

// Information about a pod failing to come up on a node.
type podStuckEvent struct {
	// Name of the cacheserver pod
	PodName string `json:"podName"`
	// Namespace of the cacheserver pod
	Namespace string `json:"namespace"`
	// State of the pod
	State PodStatus `json:"state"`
	// Timestamp when the pod got stuck
	StuckTimestamp time.Time `json:"stuckTimestamp"`
}

// Information about a node maintained by scheduler
type trackerNodeInfo struct {
	// Last known node information. This may also include nodes which have been deleted from the cluster
	NodeInfo Node `json:"nodeInfo"`

	// Last known node information timestamp
	LastUpdatedTime time.Time `json:"lastUpdatedTime"`

	// Time when node became unschedulable
	UnschedulableSince time.Time `json:"unschedulableSince"`

	// Timestamps of cacheserver pods getting stuck on this node.
	// Cleared if a pod runs successfully on the node.
	PodsStuckEvents []podStuckEvent `json:"podsStuckEvents"`
}

func isPodStuck(podState trackerPodInfo, trackerConfig TrackerConfig) bool {
	for _, stuckState := range podStuckStates {
		if podState.State == stuckState && (time.Since(podState.FirstHitTimestamp) > trackerConfig.PodStuckThresholdDuration) {
			return true
		}
	}
	return false
}

func clearOldPodStuckEvents(nodeInfo *trackerNodeInfo) {
	// Clear old events beyond retention duration
	cutoff := time.Now().Add(-PodStuckEventRetentionDuration)
	filteredEvents := []podStuckEvent{}
	for _, event := range nodeInfo.PodsStuckEvents {
		if event.StuckTimestamp.After(cutoff) {
			filteredEvents = append(filteredEvents, event)
		}
	}
	nodeInfo.PodsStuckEvents = filteredEvents
}

func addPodStuckEvent(nodeInfo *trackerNodeInfo, podState trackerPodInfo) {
	clearOldPodStuckEvents(nodeInfo)

	// Add the pod to the stuck events list
	nodeInfo.PodsStuckEvents = append(nodeInfo.PodsStuckEvents, podStuckEvent{
		PodName:        podState.PodName,
		Namespace:      podState.Namespace,
		State:          podState.State,
		StuckTimestamp: podState.LastHitTimestamp,
	})
}

func getRecentPodStuckEventCount(nodeInfo *trackerNodeInfo) int {
	clearOldPodStuckEvents(nodeInfo)
	return len(nodeInfo.PodsStuckEvents)
}

// Collection of nodes. Key is node name, value is true
type nodeList map[string]bool

// helper to add node to nodeList
func (nl nodeList) add(nodeName string) {
	nl[nodeName] = true
}

// helper to remove node from nodeList
func (nl nodeList) remove(nodeName string) {
	delete(nl, nodeName)
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

// Internal state of the tracker
type trackerInternalState struct {
	// Map of current nodes on cluster. key is the node name and value is the node information
	CurrentNodes map[string]trackerNodeInfo `json:"currentNodes"`

	// Map of namespaced-pods scheduled on cluster currently or in the past.
	// Key is namespace, value is cacheserver pods in that namespace
	PodMap map[string]trackerPodCollection `json:"podMap"`

	// PodsByCacheKey maps each Cache CR (namespace/name) to its current pods.
	// This allows GetNodesWithPods to return a global view across all CRs
	// while per-CR helpers operate on the active CR's pods only.
	PodsByCacheKey map[string][]Pod `json:"podsByCacheKey"`
}

type Tracker struct {
	mu sync.RWMutex

	Config TrackerConfig
	State  trackerInternalState
}

// RemoveCacheKey removes a Cache CR's pod entries from the tracker.
// Call this when a Cache CR is deleted so that its nodes are freed for
// scheduling by other Cache CRs.
func (t *Tracker) RemoveCacheKey(cacheKey string) {
	t.mu.Lock()
	defer t.mu.Unlock()
	for _, pod := range t.State.PodsByCacheKey[cacheKey] {
		if ns, ok := t.State.PodMap[pod.Namespace]; ok {
			delete(ns, pod.PodName)
			if len(ns) == 0 {
				delete(t.State.PodMap, pod.Namespace)
			}
		}
	}
	delete(t.State.PodsByCacheKey, cacheKey)
}

// helper function to update tracker state with current nodes
func (t *Tracker) updateNodeMap(nodes []Node) {
	// update the current nodes map
	now := time.Now()
	currentNodes := nodeList{}
	for _, node := range nodes {
		currentNodes.add(node.NodeName)

		wasNodeSchedulable := false
		nodeInfo, nodeInfoFoundOk := t.State.CurrentNodes[node.NodeName]
		if !nodeInfoFoundOk {
			log.Infof("Discovered new node %s", node.NodeName)
			nodeInfo = trackerNodeInfo{
				NodeInfo:        node,
				PodsStuckEvents: []podStuckEvent{},
			}
			if !node.Schedulable {
				// We are seeing this node for the first time and it is already
				// cordoned. We don't know when the cordon actually happened, so
				// pretend it just happened now. This way the scheduler still
				// treats the loss as short-term for one full window after a
				// controller restart, instead of giving up on the node right away.
				nodeInfo.UnschedulableSince = now
			}
		} else {
			wasNodeSchedulable = nodeInfo.NodeInfo.Schedulable
			nodeInfo.NodeInfo = node
		}

		nodeInfo.LastUpdatedTime = now
		if !node.Schedulable {
			log.Debugf("Node %s is not schedulable", node.NodeName)
			if wasNodeSchedulable {
				nodeInfo.UnschedulableSince = now
				log.Infof("Node %s became unschedulable", node.NodeName)
			}
		}

		t.State.CurrentNodes[node.NodeName] = nodeInfo
		log.WithFields(log.Fields{
			"nodeName":           node.NodeName,
			"schedulable":        node.Schedulable,
			"nodeLabels":         node.NodeLabels,
			"lastUpdated":        nodeInfo.LastUpdatedTime,
			"podStuckEventsSize": len(nodeInfo.PodsStuckEvents),
		}).Debugf("Scheduler processed node %s", node.NodeName)
	}

	// Remove nodes that are no longer present, unless they are still inside
	// the short-term node loss window (e.g. AKS surge upgrade deleted the VM
	// of a cordoned node), in which case keep the entry so pod stickiness
	// holds until the replacement VM rejoins or the window expires.
	for nodeName := range t.State.CurrentNodes {
		if _, found := currentNodes[nodeName]; found {
			continue
		}
		if t.isNodeLossShortTerm(nodeName) {
			log.Infof("Node %s is no longer present but is still within the short-term node loss window. Keeping tracker entry", nodeName)
			continue
		}
		log.Infof("Node %s is no longer present. Removing from tracker state", nodeName)
		delete(t.State.CurrentNodes, nodeName)
	}
}

func (t *Tracker) lookupPodMap(namespace string, podName string) (trackerPodInfo, bool) {
	namespacePodMap, namespaceFound := t.State.PodMap[namespace]
	if !namespaceFound {
		return trackerPodInfo{}, false
	}

	podState, podFound := namespacePodMap[podName]
	if !podFound {
		return trackerPodInfo{}, false
	}

	return podState, true
}

func (t *Tracker) updatePodMapEntry(podInfo trackerPodInfo) {
	namespacePodMap, namespaceFound := t.State.PodMap[podInfo.Namespace]
	if !namespaceFound {
		namespacePodMap = trackerPodCollection{}
	}

	namespacePodMap[podInfo.PodName] = podInfo
	t.State.PodMap[podInfo.Namespace] = namespacePodMap
}

// Helper function to update tracker state with current pods
func (t *Tracker) updatePodMap(cacheKey string, pods []Pod) {
	now := time.Now()
	t.State.PodsByCacheKey[cacheKey] = pods
	for _, pod := range pods {
		if pod.NodeName != "" {
			podState, existingPodStateFound := t.lookupPodMap(pod.Namespace, pod.PodName)
			if existingPodStateFound && podState.NodeName == pod.NodeName && podState.State == pod.State {
				podState.LastHitTimestamp = now
			} else {
				if existingPodStateFound {
					if podState.NodeName != pod.NodeName {
						log.Infof("Pod %s moved from node %s to node %s", pod.PodName, podState.NodeName, pod.NodeName)
					}

					if podState.State != pod.State {
						log.Infof("Pod %s state changed from %s to %s", pod.PodName, podState.State, pod.State)
					}
				}

				podState = trackerPodInfo{
					PodName:           pod.PodName,
					Namespace:         pod.Namespace,
					NodeName:          pod.NodeName,
					State:             pod.State,
					LastHitTimestamp:  time.Now(),
					FirstHitTimestamp: time.Now(),
				}
			}

			t.updatePodMapEntry(podState)
			log.WithFields(log.Fields{
				"podName":           pod.PodName,
				"nodeName":          pod.NodeName,
				"state":             pod.State,
				"lastHitTimestamp":  podState.LastHitTimestamp,
				"firstHitTimestamp": podState.FirstHitTimestamp,
			}).Debugf("Scheduler processed pod %s", pod.PodName)

			isPodStuck := isPodStuck(podState, t.Config)
			if isPodStuck {
				log.Warnf("Pod %s is stuck on node %s in %s state", pod.PodName, pod.NodeName, pod.State)

				stuckNodeInfo, stuckNodeFound := t.State.CurrentNodes[pod.NodeName]
				if stuckNodeFound {
					addPodStuckEvent(&stuckNodeInfo, podState)
					t.State.CurrentNodes[pod.NodeName] = stuckNodeInfo
				}
			}

			// If pod is running for more than PodStableStateDurationThreshold, clear the node's stuck events
			if pod.State == PodStatusRunning && (time.Since(podState.FirstHitTimestamp) > t.Config.PodStuckThresholdDuration) {
				nodeInfo, nodeInfoFound := t.State.CurrentNodes[pod.NodeName]
				if nodeInfoFound && len(nodeInfo.PodsStuckEvents) > 0 {
					log.Infof("Pod %s is running on node %s. Clearing node's stuck events", pod.PodName, pod.NodeName)
					nodeInfo.PodsStuckEvents = []podStuckEvent{}
					t.State.CurrentNodes[pod.NodeName] = nodeInfo
				}
			}
		}
	}
}

// Get all eligible nodes for scheduling cache pods.
func (t *Tracker) getAllEligibleNodes() (eligibleNodes []Node) {
	eligibleNodes = []Node{}

	// Loop through current nodes
	for _, trackerNodeInfo := range t.State.CurrentNodes {
		// Current schedulable nodes are eligible
		if trackerNodeInfo.NodeInfo.Schedulable {
			eligibleNodes = append(eligibleNodes, trackerNodeInfo.NodeInfo)
		}
	}

	return eligibleNodes
}

func matchesAffinity(node Node, nodeSelectorKey string, nodeSelectorValue string) bool {
	if val, ok := node.NodeLabels[nodeSelectorKey]; !ok || val != nodeSelectorValue {
		return false
	}
	return true
}

// Get eligible nodes matching affinity
func (t *Tracker) getEligibleNodesMatchingAffinity(nodeSelectorKey string, nodeSelectorValue string) (eligibleNodes []Node) {
	allEligibleNodes := t.getAllEligibleNodes()
	eligibleNodes = []Node{}

	// Filter eligible nodes based on affinity
	for _, node := range allEligibleNodes {
		if matchesAffinity(node, nodeSelectorKey, nodeSelectorValue) {
			eligibleNodes = append(eligibleNodes, node)
		}
	}

	return eligibleNodes
}

func (t *Tracker) getTotalNodeCount() (nodeCount int) {
	nodeCount = len(t.State.CurrentNodes)
	return nodeCount
}

// getNodesWithPods returns the set of nodes that currently have cache server
// pods scheduled on them across ALL Cache CRs. This prevents different CRs
// from scheduling pods onto the same node.
func (t *Tracker) getNodesWithPods() (nodesWithPods nodeList) {
	nodesWithPods = nodeList{}
	for _, pods := range t.State.PodsByCacheKey {
		for _, pod := range pods {
			if pod.NodeName != "" {
				nodesWithPods.add(pod.NodeName)
			}
		}
	}
	return nodesWithPods
}

// Helper function to check if cache pods have consistently gotten stuck on a node recently
func (t *Tracker) doesNodeHavePodErrors(nodeName string) bool {
	trackerNodeInfo, found := t.State.CurrentNodes[nodeName]
	if !found {
		return false
	}

	recentPodStuckEventCount := getRecentPodStuckEventCount(&trackerNodeInfo)
	return recentPodStuckEventCount >= SuccessivePodStuckEventThreshold
}

// getCurrentPods splits a Cache CR's pods into scheduled (have a node
// assignment) and pending (no node yet) lists.
func (t *Tracker) getCurrentPods(cacheKey string) (scheduledPods []Pod, pendingPods []Pod) {
	scheduledPods = []Pod{}
	pendingPods = []Pod{}
	for _, pod := range t.State.PodsByCacheKey[cacheKey] {
		if pod.NodeName != "" {
			scheduledPods = append(scheduledPods, pod)
		} else {
			pendingPods = append(pendingPods, pod)
		}
	}
	return scheduledPods, pendingPods
}

// Get the list of pods which are considered stuck based on their state and duration they have been in that state.
func (t *Tracker) getStuckPods(cacheKey string) []Pod {
	var stuckPods []Pod
	for _, pod := range t.State.PodsByCacheKey[cacheKey] {
		if pod.NodeName == "" {
			continue // Unscheduled pods should not be considered as stuck
		}
		podState, found := t.lookupPodMap(pod.Namespace, pod.PodName)
		if found && isPodStuck(podState, t.Config) {
			stuckPods = append(stuckPods, pod)
		}
	}
	return stuckPods
}

// Get the count of pods which are in a state which is considered for stuck evaluation (e.g. Failed, Unknown, NotReady etc).
// This includes pods which have not considered stuck yet as they have not crossed the stuck threshold duration.
// Pods waiting for short-term node loss recovery are not counted as stuck
func (t *Tracker) GetPodCountInStuckStates(cacheKey string) int {
	t.mu.RLock()
	defer t.mu.RUnlock()
	count := 0
	for _, pod := range t.State.PodsByCacheKey[cacheKey] {
		if t.isPodWaitingForNodeRecovery(pod) {
			continue
		}
		for _, stuckState := range podStuckStates {
			if pod.State == stuckState {
				count++
				break
			}
		}
	}
	return count
}

// CountPodsAwaitingNodeRecovery returns the number of Pending pods waiting for a short-term
// lost node to recover. The controller uses this to set the PodsScheduled reason
// to WaitingForNodeRecovery.
func (t *Tracker) CountPodsAwaitingNodeRecovery(cacheKey string) int {
	t.mu.RLock()
	defer t.mu.RUnlock()
	count := 0
	for _, pod := range t.State.PodsByCacheKey[cacheKey] {
		if t.isPodWaitingForNodeRecovery(pod) {
			count++
		}
	}
	return count
}

// isPodWaitingForNodeRecovery returns true when a Pending pod's previous
// node is still within the short-term loss window. The scheduler keeps such
// pods on hold rather than placing them elsewhere, so the controller reports
// them as waiting and the stuck-pods count excludes them.
func (t *Tracker) isPodWaitingForNodeRecovery(pod Pod) bool {
	if pod.State != PodStatusPending {
		return false
	}
	lastRunningNode, found := t.getLastRunningNodeForPod(pod.Namespace, pod.PodName)
	if !found || lastRunningNode == "" {
		return false
	}
	return t.isNodeLossShortTerm(lastRunningNode)
}

func (t *Tracker) getLastRunningNodeForPod(namespace string, podName string) (string, bool) {
	trackerPodInfo, found := t.lookupPodMap(namespace, podName)
	if !found || trackerPodInfo.NodeName == "" {
		return "", false
	}

	return trackerPodInfo.NodeName, true
}

func InitTracker(ctx context.Context, cfg *config.Config) *Tracker {
	tracker := Tracker{
		Config: TrackerConfig{
			PodStuckThresholdDuration:          cfg.PodStuckThresholdDuration,
			ShortTermNodeLossThresholdDuration: cfg.ShortTermNodeLossThresholdDuration,
		},
		State: trackerInternalState{
			CurrentNodes:   map[string]trackerNodeInfo{},
			PodMap:         map[string]trackerPodCollection{},
			PodsByCacheKey: map[string][]Pod{},
		},
	}
	log.Infof("Initialized tracker with config: %+v", tracker.Config)
	return &tracker
}

func (t *Tracker) updateTracker(ctx context.Context, cacheKey string, nodes []Node, pods []Pod) {
	log.Infof("Updating tracker cacheKey=%s with %d nodes and %d pods", cacheKey, len(nodes), len(pods))
	t.updateNodeMap(nodes)
	t.updatePodMap(cacheKey, pods)

	log.Infof("Tracker update complete")
}

// isNodeLossShortTerm reports whether the node has been unschedulable for less
// than the configured threshold. When true, the scheduler keeps pods Pending on
// their previous node instead of moving them, so transient outages like AKS
// rolling upgrades don't cause pod reshuffling.
func (t *Tracker) isNodeLossShortTerm(nodeName string) bool {
	trackerNodeInfo, found := t.State.CurrentNodes[nodeName]
	if !found {
		return false
	}

	if trackerNodeInfo.NodeInfo.Schedulable {
		return false
	}

	if time.Since(trackerNodeInfo.UnschedulableSince) < t.Config.ShortTermNodeLossThresholdDuration {
		return true
	}
	return false
}
