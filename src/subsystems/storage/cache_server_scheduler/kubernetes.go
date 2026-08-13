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

import (
	"context"
	"fmt"
	"time"

	log "github.com/sirupsen/logrus"
	v1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/rest"
)

// Kubernetes clientset
var clientset kubernetes.Interface

// Constants
const (
	MaxRetries                  = 3
	RetryInterval               = 5 * time.Second
	ProcessingUnitLabelValueCpu = "cpu"
	ProcessingUnitLabelValueGpu = "gpu"
	AgentpoolLabelKey           = "agentpool"
	SchedulerName               = "cacheserver-scheduler"
	CacheDataVolumeName         = "cache-data-volume"
	PodLabelSelectorStr         = "app=cacheserver"
)

func getCacheDataVolume(pod *v1.Pod) *v1.Volume {
	for i := range pod.Spec.Volumes {
		if pod.Spec.Volumes[i].Name == CacheDataVolumeName {
			return &pod.Spec.Volumes[i]
		}
	}
	return nil
}

func usesEpehemeralStorage(pod *v1.Pod) bool {
	return getCacheDataVolume(pod).EmptyDir != nil
}

func sanityCheckPod(pod *v1.Pod) bool {
	if pod.Spec.NodeName == "" && pod.Status.Phase != v1.PodPending {
		log.Errorf("Pod %s has no node assigned, but is in non-pending state %s", pod.Name, pod.Status.Phase)
		return false
	}

	if len(pod.Spec.Volumes) == 0 {
		log.Errorf("Pod %s has no volumes", pod.Name)
		return false
	}

	if getCacheDataVolume(pod) == nil {
		log.Errorf("Pod %s has no cache data volume", pod.Name)
		return false
	}

	if !usesEpehemeralStorage(pod) {
		if len(pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms) != 1 {
			log.Errorf("Pod %s should have 1 nodeselector term if it uses non-ephemeral storage", pod.Name)
			return false
		}

		nodeSelectorTerm := pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms[0]
		if len(nodeSelectorTerm.MatchExpressions) != 1 {
			log.Errorf("Pod %s should have 1 nodeAffinity match expression if it uses non-ephemeral storage", pod.Name)
			return false
		}

		matchExpression := nodeSelectorTerm.MatchExpressions[0]
		if matchExpression.Key != GetConfig().ProcessingUnitLabelKey && matchExpression.Key != AgentpoolLabelKey {
			log.Errorf("Pod %s should have match expression for processing unit or agentpool if it uses non-ephemeral storage", pod.Name)
			return false
		}

		if matchExpression.Operator != v1.NodeSelectorOpIn {
			log.Errorf("Pod %s should have match expression with operator In if it uses non-ephemeral storage", pod.Name)
			return false
		}
	} else {
		// Ephemeral storage pods should not use custom scheduler
		if pod.Spec.SchedulerName == SchedulerName {
			log.Errorf("Pod %s should not have custom scheduler name if it uses ephemeral storage", pod.Name)
			return false
		}
	}

	return true
}

func getPodAllowedProcessingUnits(pod *v1.Pod) []string {
	allowedProcessingUnits := []string{}
	for _, term := range pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms {
		for _, expression := range term.MatchExpressions {
			if expression.Key == GetConfig().ProcessingUnitLabelKey {
				allowedProcessingUnits = append(allowedProcessingUnits, expression.Values...)
			}
		}
	}
	return allowedProcessingUnits
}

func getPodAllowedAgentpools(pod *v1.Pod) []string {
	allowedAgentpools := []string{}
	for _, term := range pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms {
		for _, expression := range term.MatchExpressions {
			if expression.Key == AgentpoolLabelKey {
				allowedAgentpools = append(allowedAgentpools, expression.Values...)
			}
		}
	}
	return allowedAgentpools
}

func getPodState(pod *v1.Pod) PodStatus {
	if pod.ObjectMeta.DeletionTimestamp != nil {
		log.Infof("Pod %s is terminating", pod.Name)
		return PodStatusTerminating
	}

	for _, containerStatus := range pod.Status.ContainerStatuses {
		if !containerStatus.Ready {
			log.Infof("Pod %s has container %s not ready", pod.Name, containerStatus.Name)
			return PodStatusNotReady
		}
	}

	switch pod.Status.Phase {
	case v1.PodPending:
		return PodStatusPending
	case v1.PodRunning:
		return PodStatusRunning
	case v1.PodSucceeded:
		return PodStatusSucceeded
	case v1.PodFailed:
		return PodStatusFailed
	case v1.PodUnknown:
		return PodStatusUnknown
	default:
		log.Errorf("Unknown pod state: %s", pod.Status.Phase)
		return PodStatusUnknown
	}
}

// lazyInit initializes the kubernetes clientset
func lazyInit() {
	if clientset == nil {
		// creates the in-cluster config
		config, err := rest.InClusterConfig()
		if err != nil {
			log.Fatalf("Error creating in-cluster config: %s", err.Error())
		}

		// creates the clientset
		clientset, err = kubernetes.NewForConfig(config) // Assign the value to the existing clientset variable
		if err != nil {
			log.Fatalf("Error creating clientset: %s", err.Error())
		}
	}
}

func GetNodesFromK8s() ([]Node, error) {
	lazyInit()

	nodes := &v1.NodeList{}
	retryCount := 0
	for retryCount < MaxRetries {
		var err error
		nodes, err = clientset.CoreV1().Nodes().List(context.TODO(), metav1.ListOptions{})
		if err != nil {
			log.Warnf("Kubernetes client encountered error getting nodes: %s", err.Error())
			if retryCount < MaxRetries {
				log.Warnf("Retrying k8s get nodes in %s", RetryInterval)
				retryCount++
				time.Sleep(RetryInterval)
				continue
			} else {
				log.Errorf("Max retries exceeded for k8s get nodes")
				return nil, err
			}
		} else {
			break
		}
	}

	nodeList := []Node{}
	for i := range nodes.Items {
		node := &nodes.Items[i]
		nodeObj := Node{
			NodeName:       node.Name,
			Schedulable:    !node.Spec.Unschedulable,
			ProcessingUnit: node.Labels[GetConfig().ProcessingUnitLabelKey],
			Agentpool:      node.Labels[AgentpoolLabelKey],
			Allocatable:    node.Status.Allocatable,
		}
		nodeList = append(nodeList, nodeObj)
	}

	return nodeList, nil
}

func GetPodsFromK8s() ([]Pod, error) {
	lazyInit()

	pods := &v1.PodList{}
	retryCount := 0
	for retryCount < MaxRetries {
		var err error
		pods, err = clientset.CoreV1().Pods(GetConfig().CacheserverNamespace).List(
			context.TODO(),
			metav1.ListOptions{
				LabelSelector: PodLabelSelectorStr,
			})
		if err != nil {
			log.Errorf("Kubernetes client encountered error getting pods: %s", err.Error())
			if retryCount < MaxRetries {
				log.Warnf("Retrying k8s get pods in %s", RetryInterval)
				retryCount++
				time.Sleep(RetryInterval)
				continue
			} else {
				log.Errorf("Max retries exceeded for k8s get pods")
				return nil, err
			}
		} else {
			break
		}
	}

	podList := []Pod{}
	for i := range pods.Items {
		pod := &pods.Items[i]
		if !sanityCheckPod(pod) {
			log.Errorf("Pod %s failed sanity check. Ignoring", pod.Name)
			continue
		}

		podObj := Pod{
			PodName:                pod.Name,
			NodeName:               pod.Spec.NodeName,
			State:                  getPodState(pod),
			SchedulerBypassed:      pod.Spec.SchedulerName != SchedulerName,
			AllowedProcessingUnits: getPodAllowedProcessingUnits(pod),
			AllowedAgentpools:      getPodAllowedAgentpools(pod),
			UseEphemeralStorage:    pod.Spec.Volumes[0].EmptyDir != nil,
			Requests:               pod.Spec.Containers[0].Resources.Requests,
		}
		podList = append(podList, podObj)
	}

	return podList, nil
}

func executeDeleteInstruction(podName string) error {
	deletionGracePeriodSeconds := int64(0)
	err := clientset.CoreV1().Pods(GetConfig().CacheserverNamespace).Delete(
		context.TODO(),
		podName,
		metav1.DeleteOptions{
			GracePeriodSeconds: &deletionGracePeriodSeconds,
		})

	if err != nil {
		log.Errorf("Error deleting pod %s: %s", podName, err.Error())
		return err
	}

	log.Infof("Deleted pod %s", podName)
	return nil
}

func executeBindInstuction(podName string, destinationNode string) error {
	config := GetConfig()

	err := clientset.CoreV1().Pods(config.CacheserverNamespace).Bind(
		context.TODO(),
		&v1.Binding{
			ObjectMeta: metav1.ObjectMeta{
				Name:      podName,
				Namespace: config.CacheserverNamespace,
			},
			Target: v1.ObjectReference{
				APIVersion: "v1",
				Kind:       "Node",
				Name:       destinationNode,
			},
		},
		metav1.CreateOptions{},
	)

	if err != nil {
		log.Errorf("Error binding pod %s to node %s: %s", podName, destinationNode, err.Error())
		return err
	}

	log.Infof("Bound pod %s to node %s", podName, destinationNode)
	return nil
}

func emitFailedSchedulingEvent(podName string, reason string) error {
	log.Warnf("Emitting event for pod %s schedule failure due to %s", podName, reason)
	event := &v1.Event{
		ObjectMeta: metav1.ObjectMeta{
			Name:      podName,
			Namespace: GetConfig().CacheserverNamespace,
		},
		InvolvedObject: v1.ObjectReference{
			Kind:      "Pod",
			Name:      podName,
			Namespace: GetConfig().CacheserverNamespace,
		},
		Reason:        "FailedScheduling",
		Message:       fmt.Sprintf("Failed to schedule pod %s. %s", podName, reason),
		Type:          v1.EventTypeWarning,
		LastTimestamp: metav1.Now(),
		Source: v1.EventSource{
			Component: SchedulerName,
		},
	}

	_, err := clientset.CoreV1().Events(GetConfig().CacheserverNamespace).Create(context.TODO(), event, metav1.CreateOptions{})
	if err != nil {
		log.Errorf("Error emitting event for pod %s: %s", podName, err.Error())
		return err
	}

	log.Infof("Emitted event for pod %s", podName)
	return nil
}

func ExecuteSchedulerInstructionsOnK8s(instructions []SchedulerInstruction) []error {
	lazyInit()

	errors := []error{}
	for _, instruction := range instructions {
		if instruction.Type == DeleteInstruction {
			log.WithFields(log.Fields{
				"podName": instruction.PodName,
				"reason":  instruction.Reason,
			}).Info("Executing delete pod instruction")

			err := executeDeleteInstruction(instruction.PodName)
			if err != nil {
				errors = append(errors, err)
			}
		} else if instruction.Type == BindInstruction {
			log.WithFields(log.Fields{
				"podName":         instruction.PodName,
				"destinationNode": instruction.DestinationNode,
				"reason":          instruction.Reason,
			}).Info("Executing bind pod instruction")

			if instruction.DestinationNode != "" {
				err := executeBindInstuction(instruction.PodName, instruction.DestinationNode)
				if err != nil {
					errors = append(errors, err)
				}
			} else {
				err := emitFailedSchedulingEvent(instruction.PodName, instruction.Reason)
				if err != nil {
					errors = append(errors, err)
				}
			}
		} else {
			log.Errorf("Unknown instruction type: %s", instruction.Type)
		}
	}

	return errors
}

// GetDesiredReplicaCountFromK8s fetches desired replica count from K8s StatefulSet
func GetDesiredReplicaCountFromK8s(namespace string) (int, error) {
	lazyInit()
	statefulset, err := clientset.AppsV1().StatefulSets(namespace).Get(context.TODO(), "cacheserver", metav1.GetOptions{})
	if err != nil {
		log.Warnf("Error getting StatefulSet 'cacheserver' in namespace %s: %v", namespace, err)
		return 0, err
	}

	if statefulset.Spec.Replicas == nil {
		return 0, fmt.Errorf("StatefulSet %s has no replicas specified", statefulset.Name)
	}

	replicas := int(*statefulset.Spec.Replicas)
	log.Debugf("Found StatefulSet %s with desired replicas: %d", statefulset.Name, replicas)
	return replicas, nil
}
