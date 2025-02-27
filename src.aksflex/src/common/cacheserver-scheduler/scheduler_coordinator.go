package main

import log "github.com/sirupsen/logrus"

type PodInfo struct {
	PodName      string `json:"podName"`
	PodNamespace string `json:"podNamespace"`
}

// Global NodePodMapper defines an interface for managing Node-Pod mappings.
type SchedulerCoordinator interface {
	getNamespaceLocks(podNamespace string) map[string]bool

	getAllNamespacesLocks() map[string]bool

	acquireLock(nodeName, podName string, namespace string) bool

	releaseLock(nodeName, namespace string) bool
}

var coordinator SchedulerCoordinator

// Initialize SchedulerCoordinator to whichever implementation desired.
// Currently, k8 ConfigMap-based SchedulerCoordinator is supported.
func InitSchedulerCoordinator() {

	if coordinator == nil {
		coordinator = InitConfigMapSchedulerCoordinator()
		log.Infof("Initialized SchedulerCoordinator")
	} else {
		log.Debugf("SchedulerCoordinator is already initialized")
	}
}
