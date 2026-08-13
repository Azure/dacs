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
