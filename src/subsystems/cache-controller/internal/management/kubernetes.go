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
	"fmt"
	"time"

	log "github.com/sirupsen/logrus"
	appsv1 "k8s.io/api/apps/v1"
	v1 "k8s.io/api/core/v1"
	apierrors "k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	ctrlclient "sigs.k8s.io/controller-runtime/pkg/client"
)

// Constants
const (
	MaxRetries            = 3
	RetryInterval         = 5 * time.Second
	ControllerName        = "cache-controller"
	CacheDataVolumeName   = "cache-data-volume"
	PodLabelSelectorKey   = "app"
	PodLabelSelectorValue = "cacheserver"
	StatefulSetName       = "cacheserver"

	// CacheNameLabelKey is the label key added to StatefulSets and pods to
	// identify the owning Cache CR by name.
	CacheNameLabelKey = "dacs.azure.com/cache-name"
)

func buildNodeListFromK8sNodes(nodes []v1.Node) []Node {
	nodeList := make([]Node, 0, len(nodes))
	for i := range nodes {
		node := &nodes[i]
		nodeObj := Node{
			NodeName:    node.Name,
			Schedulable: !node.Spec.Unschedulable,
			NodeLabels:  node.Labels,
			Allocatable: node.Status.Allocatable,
		}
		nodeList = append(nodeList, nodeObj)
	}

	return nodeList
}

func buildPodListFromK8sPods(pods []v1.Pod) []Pod {
	var podList []Pod
	for i := range pods {
		pod := &pods[i]
		if !sanityCheckPod(pod) {
			log.Errorf("Pod %s(%s) failed sanity check. Ignoring", pod.Name, pod.Namespace)
			continue
		}

		podObj := Pod{
			PodName:             pod.Name,
			Namespace:           pod.Namespace,
			NodeName:            pod.Spec.NodeName,
			State:               getPodState(pod),
			SchedulerBypassed:   pod.Spec.SchedulerName != ControllerName,
			NodeSelectorKey:     "",
			NodeSelectorValue:   "",
			UseEphemeralStorage: usesEphemeralStorage(pod),
			Requests:            pod.Spec.Containers[0].Resources.Requests,
		}

		if pod.Spec.Affinity != nil && pod.Spec.Affinity.NodeAffinity != nil &&
			pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution != nil {
			podObj.NodeSelectorKey = getPodNodeSelectorKey(pod)
			podObj.NodeSelectorValue = getPodNodeSelectorValue(pod)
		}
		podList = append(podList, podObj)
	}

	return podList
}

// BuildPodList converts raw Kubernetes pods into the internal Pod representation.
func BuildPodList(pods []v1.Pod) []Pod {
	return buildPodListFromK8sPods(pods)
}

func getCacheDataVolume(pod *v1.Pod) *v1.Volume {
	for i := range pod.Spec.Volumes {
		if pod.Spec.Volumes[i].Name == CacheDataVolumeName {
			return &pod.Spec.Volumes[i]
		}
	}
	return nil
}

func usesEphemeralStorage(pod *v1.Pod) bool {
	return getCacheDataVolume(pod).EmptyDir != nil
}

func sanityCheckPod(pod *v1.Pod) bool {
	if pod.Spec.NodeName == "" && pod.Status.Phase != v1.PodPending {
		log.Errorf("Pod %s(%s) has no node assigned, but is in non-pending state %s", pod.Name, pod.Namespace, pod.Status.Phase)
		return false
	}

	if len(pod.Spec.Volumes) == 0 {
		log.Errorf("Pod %s(%s) has no volumes", pod.Name, pod.Namespace)
		return false
	}

	if getCacheDataVolume(pod) == nil {
		log.Errorf("Pod %s(%s) has no cache data volume", pod.Name, pod.Namespace)
		return false
	}

	if !usesEphemeralStorage(pod) {
		if pod.Spec.Affinity == nil ||
			pod.Spec.Affinity.NodeAffinity == nil ||
			pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution == nil ||
			len(pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms) != 1 {
			log.Errorf("Pod %s(%s) should have 1 nodeselector term if it uses non-ephemeral storage", pod.Name, pod.Namespace)
			return false
		}

		nodeSelectorTerm := pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms[0]
		if len(nodeSelectorTerm.MatchExpressions) != 1 {
			log.Errorf("Pod %s(%s) should have 1 nodeAffinity match expression if it uses non-ephemeral storage", pod.Name, pod.Namespace)
			return false
		}

		matchExpression := nodeSelectorTerm.MatchExpressions[0]
		if matchExpression.Operator != v1.NodeSelectorOpIn {
			log.Errorf("Pod %s(%s) should have match expression with operator In if it uses non-ephemeral storage", pod.Name, pod.Namespace)
			return false
		}

		if matchExpression.Values == nil || len(matchExpression.Values) != 1 {
			log.Errorf("Pod %s(%s) should have match expression with exactly 1 value if it uses non-ephemeral storage", pod.Name, pod.Namespace)
			return false
		}
	} else {
		// Ephemeral storage pods should not use custom scheduler
		if pod.Spec.SchedulerName == ControllerName {
			log.Errorf("Pod %s(%s) should not have custom scheduler name if it uses ephemeral storage", pod.Name, pod.Namespace)
			return false
		}
	}

	return true
}

func getPodNodeSelectorKey(pod *v1.Pod) string {
	return pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms[0].MatchExpressions[0].Key
}

func getPodNodeSelectorValue(pod *v1.Pod) string {
	return pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms[0].MatchExpressions[0].Values[0]
}

func getPodState(pod *v1.Pod) PodStatus {
	if pod.ObjectMeta.DeletionTimestamp != nil {
		log.Infof("Pod %s(%s) is terminating", pod.Name, pod.Namespace)
		return PodStatusTerminating
	}

	for _, containerStatus := range pod.Status.ContainerStatuses {
		if !containerStatus.Ready {
			log.Infof("Pod %s(%s) has container %s not ready", pod.Name, pod.Namespace, containerStatus.Name)
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
		log.Errorf("Unknown pod state: %s for pod %s(%s)", pod.Status.Phase, pod.Name, pod.Namespace)
		return PodStatusUnknown
	}
}

func GetNodesFromCache(ctx context.Context, reader ctrlclient.Reader) ([]Node, error) {
	nodes := &v1.NodeList{}
	if err := reader.List(ctx, nodes); err != nil {
		log.Errorf("Error listing cached nodes: %s", err.Error())
		return nil, err
	}

	return buildNodeListFromK8sNodes(nodes.Items), nil
}

func GetPodsFromCache(ctx context.Context, reader ctrlclient.Reader) ([]Pod, error) {
	pods := &v1.PodList{}
	if err := reader.List(ctx, pods, ctrlclient.MatchingLabels{PodLabelSelectorKey: PodLabelSelectorValue}); err != nil {
		log.Errorf("Error listing cached pods: %s", err.Error())
		return nil, err
	}

	return buildPodListFromK8sPods(pods.Items), nil
}

func executeDeleteInstruction(ctx context.Context, k8sClient ctrlclient.Client, namespace string, podName string) error {
	pod := &v1.Pod{
		ObjectMeta: metav1.ObjectMeta{
			Name:      podName,
			Namespace: namespace,
		},
	}
	err := k8sClient.Delete(ctx, pod, ctrlclient.GracePeriodSeconds(0))

	if err != nil {
		log.Errorf("Error deleting pod %s(%s): %s", podName, namespace, err.Error())
		return err
	}

	log.Infof("Deleted pod %s(%s)", podName, namespace)
	return nil
}

func executeBindInstruction(ctx context.Context, k8sClient ctrlclient.Client, namespace string, podName string, destinationNode string) error {
	pod := &v1.Pod{
		ObjectMeta: metav1.ObjectMeta{
			Name:      podName,
			Namespace: namespace,
		},
	}
	binding := &v1.Binding{
		ObjectMeta: metav1.ObjectMeta{
			Name:      podName,
			Namespace: namespace,
		},
		Target: v1.ObjectReference{
			APIVersion: "v1",
			Kind:       "Node",
			Name:       destinationNode,
		},
	}
	err := k8sClient.SubResource("binding").Create(ctx, pod, binding)

	if err != nil {
		log.Errorf("Error binding pod %s(%s) to node %s: %s", podName, namespace, destinationNode, err.Error())
		return err
	}

	log.Infof("Bound pod %s(%s) to node %s", podName, namespace, destinationNode)
	return nil
}

func emitFailedSchedulingEvent(ctx context.Context, k8sClient ctrlclient.Client, namespace string, podName string, reason string) error {
	log.Warnf("Emitting event for pod %s(%s) schedule failure due to %s", podName, namespace, reason)
	event := &v1.Event{
		ObjectMeta: metav1.ObjectMeta{
			Name:      podName,
			Namespace: namespace,
		},
		InvolvedObject: v1.ObjectReference{
			Kind:      "Pod",
			Name:      podName,
			Namespace: namespace,
		},
		Reason:        "FailedScheduling",
		Message:       fmt.Sprintf("Failed to schedule pod %s. %s", podName, reason),
		Type:          v1.EventTypeWarning,
		LastTimestamp: metav1.Now(),
		Source: v1.EventSource{
			Component: ControllerName,
		},
	}

	err := k8sClient.Create(ctx, event)
	if err != nil {
		// Ignore already exists error
		if !apierrors.IsAlreadyExists(err) {
			log.Errorf("Error emitting event for pod %s(%s): %s", podName, namespace, err.Error())
			return err
		}
	}

	log.Infof("Emitted event for pod %s(%s)", podName, namespace)
	return nil
}

func ExecuteSchedulerInstructionsOnK8s(ctx context.Context, k8sClient ctrlclient.Client, instructions []SchedulerInstruction) []error {
	errors := []error{}
	for _, instruction := range instructions {
		if instruction.Type == DeleteInstruction {
			log.WithFields(log.Fields{
				"namespace": instruction.Namespace,
				"podName":   instruction.PodName,
				"reason":    instruction.Reason,
			}).Info("Executing delete pod instruction")

			err := executeDeleteInstruction(ctx, k8sClient, instruction.Namespace, instruction.PodName)
			if err != nil {
				errors = append(errors, err)
			}
		} else if instruction.Type == BindInstruction {
			log.WithFields(log.Fields{
				"namespace":       instruction.Namespace,
				"podName":         instruction.PodName,
				"destinationNode": instruction.DestinationNode,
				"reason":          instruction.Reason,
			}).Info("Executing bind pod instruction")

			if instruction.DestinationNode != "" {
				err := executeBindInstruction(ctx, k8sClient, instruction.Namespace, instruction.PodName, instruction.DestinationNode)
				if err != nil {
					errors = append(errors, err)
				}
			} else {
				err := emitFailedSchedulingEvent(ctx, k8sClient, instruction.Namespace, instruction.PodName, instruction.Reason)
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
func GetDesiredReplicaCountFromK8s(ctx context.Context, k8sClient ctrlclient.Client, namespace string) (int, error) {
	statefulset := &appsv1.StatefulSet{}
	err := k8sClient.Get(ctx, ctrlclient.ObjectKey{Namespace: namespace, Name: StatefulSetName}, statefulset)
	if err != nil {
		log.Warnf("Error getting StatefulSet '%s' in namespace %s: %v", StatefulSetName, namespace, err)
		return 0, err
	}

	if statefulset.Spec.Replicas == nil {
		return 0, fmt.Errorf("StatefulSet %s has no replicas specified", statefulset.Name)
	}

	replicas := int(*statefulset.Spec.Replicas)
	log.Debugf("Found StatefulSet %s with desired replicas: %d", statefulset.Name, replicas)
	return replicas, nil
}
