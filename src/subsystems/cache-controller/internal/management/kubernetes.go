package management

import (
	"context"
	"fmt"
	"time"

	log "github.com/sirupsen/logrus"
	v1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"
)

// Constants
const (
	MaxRetries          = 3
	RetryInterval       = 5 * time.Second
	ControllerName      = "cache-controller"
	CacheDataVolumeName = "cache-data-volume"
	PodLabelSelectorStr = "app=cacheserver"
	StatefulSetName     = "cacheserver"
)

func getCacheDataVolume(pod v1.Pod) *v1.Volume {
	for _, volume := range pod.Spec.Volumes {
		if volume.Name == CacheDataVolumeName {
			return &volume
		}
	}
	return nil
}

func usesEphemeralStorage(pod v1.Pod) bool {
	return getCacheDataVolume(pod).EmptyDir != nil
}

func sanityCheckPod(pod v1.Pod) bool {
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

func getPodNodeSelectorKey(pod v1.Pod) string {
	return pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms[0].MatchExpressions[0].Key
}

func getPodNodeSelectorValue(pod v1.Pod) string {
	return pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms[0].MatchExpressions[0].Values[0]
}

func getPodState(pod v1.Pod) PodStatus {
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

func GetNodesFromK8s(clientset kubernetes.Interface) ([]Node, error) {
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
	for _, node := range nodes.Items {
		nodeObj := Node{
			NodeName:    node.Name,
			Schedulable: !node.Spec.Unschedulable,
			NodeLabels:  node.Labels,
		}
		nodeList = append(nodeList, nodeObj)
	}

	return nodeList, nil
}

func GetPodsFromK8s(clientset kubernetes.Interface) ([]Pod, error) {
	pods := &v1.PodList{}
	retryCount := 0
	for retryCount < MaxRetries {
		var err error
		pods, err = clientset.CoreV1().Pods("").List(
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
	for _, pod := range pods.Items {
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
			NodeSelectorKey:     getPodNodeSelectorKey(pod),
			NodeSelectorValue:   getPodNodeSelectorValue(pod),
			UseEphemeralStorage: usesEphemeralStorage(pod),
		}
		podList = append(podList, podObj)
	}

	return podList, nil
}

func executeDeleteInstruction(clientset kubernetes.Interface, namespace string, podName string) error {
	deletionGracePeriodSeconds := int64(0)
	err := clientset.CoreV1().Pods(namespace).Delete(
		context.TODO(),
		podName,
		metav1.DeleteOptions{
			GracePeriodSeconds: &deletionGracePeriodSeconds,
		})

	if err != nil {
		log.Errorf("Error deleting pod %s(%s): %s", podName, namespace, err.Error())
		return err
	}

	log.Infof("Deleted pod %s(%s)", podName, namespace)
	return nil
}

func executeBindInstruction(clientset kubernetes.Interface, namespace string, podName string, destinationNode string) error {
	err := clientset.CoreV1().Pods(namespace).Bind(
		context.TODO(),
		&v1.Binding{
			ObjectMeta: metav1.ObjectMeta{
				Name:      podName,
				Namespace: namespace,
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
		log.Errorf("Error binding pod %s(%s) to node %s: %s", podName, namespace, destinationNode, err.Error())
		return err
	}

	log.Infof("Bound pod %s(%s) to node %s", podName, namespace, destinationNode)
	return nil
}

func emitFailedSchedulingEvent(clientset kubernetes.Interface, namespace string, podName string, reason string) error {
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

	_, err := clientset.CoreV1().Events(namespace).Create(context.TODO(), event, metav1.CreateOptions{})
	if err != nil {
		// Ignore already exists error
		if err.Error() != fmt.Sprintf("events \"%s\" already exists", podName) {
			log.Errorf("Error emitting event for pod %s(%s): %s", podName, namespace, err.Error())
			return err
		}
	}

	log.Infof("Emitted event for pod %s(%s)", podName, namespace)
	return nil
}

func ExecuteSchedulerInstructionsOnK8s(clientset kubernetes.Interface, instructions []SchedulerInstruction) []error {
	errors := []error{}
	for _, instruction := range instructions {
		if instruction.Type == DeleteInstruction {
			log.WithFields(log.Fields{
				"namespace": instruction.Namespace,
				"podName":   instruction.PodName,
				"reason":    instruction.Reason,
			}).Info("Executing delete pod instruction")

			err := executeDeleteInstruction(clientset, instruction.Namespace, instruction.PodName)
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
				err := executeBindInstruction(clientset, instruction.Namespace, instruction.PodName, instruction.DestinationNode)
				if err != nil {
					errors = append(errors, err)
				}
			} else {
				err := emitFailedSchedulingEvent(clientset, instruction.Namespace, instruction.PodName, instruction.Reason)
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
func GetDesiredReplicaCountFromK8s(clientset kubernetes.Interface, namespace string) (int, error) {
	statefulset, err := clientset.AppsV1().StatefulSets(namespace).Get(context.TODO(), StatefulSetName, metav1.GetOptions{})
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
