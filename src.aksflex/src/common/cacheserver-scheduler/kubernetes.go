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
var clientset *kubernetes.Clientset

// Constants
const (
	MaxRetries                  = 3
	RetryInterval               = 5 * time.Second
	ProcessingUnitLabelKey      = "dacs.azure.com/processing-unit"
	ProcessingUnitLabelValueCpu = "cpu"
	ProcessingUnitLabelValueGpu = "gpu"
	AgentpoolLabelKey           = "agentpool"
	SchedulerName               = "cacheserver-scheduler"
	CacheDataVolumeName         = "cache-data-volume"
	CacheserverNamespace        = "azureml"
	PodLabelSelectorStr         = "app=cacheserver"
)

func getCacheDataVolume(pod v1.Pod) *v1.Volume {
	for _, volume := range pod.Spec.Volumes {
		if volume.Name == CacheDataVolumeName {
			return &volume
		}
	}
	return nil
}

func usesEpehemeralStorage(pod v1.Pod) bool {
	return getCacheDataVolume(pod).EmptyDir != nil
}

func sanityCheckPod(pod v1.Pod) bool {
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
		if matchExpression.Key != ProcessingUnitLabelKey && matchExpression.Key != AgentpoolLabelKey {
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

func getPodAllowedProcessingUnits(pod v1.Pod) []string {
	allowedProcessingUnits := []string{}
	for _, term := range pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms {
		for _, expression := range term.MatchExpressions {
			if expression.Key == ProcessingUnitLabelKey {
				allowedProcessingUnits = append(allowedProcessingUnits, expression.Values...)
			}
		}
	}
	return allowedProcessingUnits
}

func getPodAllowedAgentpools(pod v1.Pod) []string {
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
	for _, node := range nodes.Items {
		nodeObj := Node{
			NodeName:       node.Name,
			Schedulable:    !node.Spec.Unschedulable,
			ProcessingUnit: node.Labels[ProcessingUnitLabelKey],
			Agentpool:      node.Labels[AgentpoolLabelKey],
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
		pods, err = clientset.CoreV1().Pods(CacheserverNamespace).List(
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
			log.Errorf("Pod %s failed sanity check. Ignoring", pod.Name)
			continue
		}

		podObj := Pod{
			PodName:                pod.Name,
			NodeName:               pod.Spec.NodeName,
			State:                  pod.Status.Phase,
			SchedulerBypassed:      pod.Spec.SchedulerName != SchedulerName,
			AllowedProcessingUnits: getPodAllowedProcessingUnits(pod),
			AllowedAgentpools:      getPodAllowedAgentpools(pod),
			UseEphemeralStorage:    pod.Spec.Volumes[0].EmptyDir != nil,
		}
		podList = append(podList, podObj)
	}

	return podList, nil
}

func executeDeleteInstruction(podName string) error {
	deletionGracePeriodSeconds := int64(0)
	err := clientset.CoreV1().Pods(CacheserverNamespace).Delete(
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
	err := clientset.CoreV1().Pods(CacheserverNamespace).Bind(
		context.TODO(),
		&v1.Binding{
			ObjectMeta: metav1.ObjectMeta{
				Name:      podName,
				Namespace: CacheserverNamespace,
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
			Namespace: CacheserverNamespace,
		},
		InvolvedObject: v1.ObjectReference{
			Kind:      "Pod",
			Name:      podName,
			Namespace: CacheserverNamespace,
		},
		Reason:        "FailedScheduling",
		Message:       fmt.Sprintf("Failed to schedule pod %s. %s", podName, reason),
		Type:          v1.EventTypeWarning,
		LastTimestamp: metav1.Now(),
		Source: v1.EventSource{
			Component: SchedulerName,
		},
	}

	_, err := clientset.CoreV1().Events(CacheserverNamespace).Create(context.TODO(), event, metav1.CreateOptions{})
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
