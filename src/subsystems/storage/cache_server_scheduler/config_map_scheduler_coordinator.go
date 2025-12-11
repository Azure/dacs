package main

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"
	"time"

	log "github.com/sirupsen/logrus"
	v1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

// Global variables for ConfigMap name and namespace
var (
	configMapName      string = "node-cacheserver-mapping"
	configMapNamespace string = "default"
	configMapKey       string = "NodeCacheServerMapping"
)

// ConfigMapSchedulerCoordinator is the implementation of SchedulerCoordinator interface using Kubernetes ConfigMaps.
type ConfigMapSchedulerCoordinator struct {
}

// InitConfigMapSchedulerCoordinator creates a new ConfigMap-based SchedulerCoordinator implementation.
func InitConfigMapSchedulerCoordinator() *ConfigMapSchedulerCoordinator {
	// Ensure Kubernetes clientset is initialized.
	lazyInit()
	return &ConfigMapSchedulerCoordinator{}
}

// loadConfigMap loads the ConfigMap from Kubernetes.
// If the ConfigMap does not exist, it creates a new one with an empty Node-PodInfo mapping.
func loadConfigMap() (*v1.ConfigMap, error) {
	lazyInit()

	configMap, err := clientset.CoreV1().ConfigMaps(configMapNamespace).Get(context.TODO(), configMapName, metav1.GetOptions{})
	if err != nil {
		if errors.IsNotFound(err) {
			log.Warnf("ConfigMap '%s' not found, creating a new one", configMapName)
			configMap = &v1.ConfigMap{
				ObjectMeta: metav1.ObjectMeta{
					Name:      configMapName,
					Namespace: configMapNamespace,
				},
				Data: map[string]string{
					configMapKey: "{}",
				},
			}
			configMap, err = clientset.CoreV1().ConfigMaps(configMapNamespace).Create(context.TODO(), configMap, metav1.CreateOptions{})
			if err != nil {
				log.Fatalf("Failed to create ConfigMap: %v", err)
			}
			log.Infof("Successfully created ConfigMap '%s'", configMapName)
		} else {
			log.Fatalf("Failed to load ConfigMap: %v", err)
		}
	} else {
		log.Debugf("Successfully loaded ConfigMap '%s'", configMapName)
	}

	return configMap, nil
}

// getNodePodMapping retrieves the Node-PodInfo mapping for the specified node from the ConfigMap.
// If no node is specified, returns all Node-PodInfo mappings.
func getNodePodMapping() (map[string]PodInfo, error) {
	configMap, err := loadConfigMap()
	if err != nil {
		return nil, err
	}

	mappings := make(map[string]PodInfo)
	data, ok := configMap.Data[configMapKey]
	if !ok || data == "" {
		return nil, fmt.Errorf("key %s not found in configMap", configMapKey)
	}

	if err := json.Unmarshal([]byte(data), &mappings); err != nil {
		return nil, err
	}
	return mappings, nil
}

// updateNodePodMapping updates the Node-PodInfo mapping in the ConfigMap.
func updateNodePodMapping(node string, podInfo PodInfo) error {
	return updateConfigMapWithRetry(node, podInfo)
}

// deleteNodePodMapping deletes the Node-PodInfo mapping from the ConfigMap.
func deleteNodePodMapping(node string, podNamespace string) error {
	return deleteConfigMapWithRetry(node, podNamespace)
}

func updateConfigMapWithRetry(node string, podInfo PodInfo) error {
	return retryConfigMapOperation(func(configMap *v1.ConfigMap) error {
		return updateConfigMapData(configMap, node, podInfo)
	})
}

func deleteConfigMapWithRetry(node string, podNamespace string) error {
	return retryConfigMapOperation(func(configMap *v1.ConfigMap) error {
		return deleteConfigMapData(configMap, node, podNamespace)
	})
}

// updateConfigMapData updates a specific Node-PodInfo pair in the ConfigMap's data.
// If the node does not exist, it adds a new Node-PodInfo pair.
// If node already exists, it returns an error indicating the node is already occupied.
func updateConfigMapData(configMap *v1.ConfigMap, node string, podInfo PodInfo) error {
	mappings := make(map[string]PodInfo)
	log.Debugf("Updating ConfigMap '%s' with node '%s' and pod '%s' in namespace '%s'", configMap.Name, node, podInfo.PodName, podInfo.PodNamespace)
	// Check if the ConfigMap contains "NodeCacheServerMapping" data
	if data, exists := configMap.Data[configMapKey]; exists && data != "" {
		err := json.Unmarshal([]byte(data), &mappings)
		if err != nil {
			return err
		}
	} else {
		return fmt.Errorf("key '%s' not found in ConfigMap '%s'", configMapKey, configMap.Name)
	}

	// Check if the node already exists and is not empty
	if existingValue, exists := mappings[node]; exists && existingValue.PodName != "" {
		log.Warnf("Node '%s' is already occupied by pod '%s' in namespace: '%s'", node, existingValue.PodName, existingValue.PodNamespace)
		return fmt.Errorf("Node already occupied: node '%s' is mapped to '%s' in namespace: '%s'", node, existingValue.PodName, existingValue.PodNamespace)
	}

	// If the node is not occupied, proceed with the update
	mappings[node] = podInfo

	updatedData, err := json.Marshal(mappings)
	if err != nil {
		return err
	}

	// Update the ConfigMap with the new JSON data
	configMap.Data[configMapKey] = string(updatedData)
	return nil
}

// deleteConfigMapData deletes a specific Node-PodInfo pair from the ConfigMap's data.
// If the node does not exist, it returns without any operation.
// If the node exists but podNamespace does not match, it returns an error.
func deleteConfigMapData(configMap *v1.ConfigMap, node string, podNamespace string) error {
	mappings := make(map[string]PodInfo)

	// Check if the ConfigMap contains "NodeCacheServerMapping" data
	if data, exists := configMap.Data[configMapKey]; exists && data != "" {
		err := json.Unmarshal([]byte(data), &mappings)
		if err != nil {
			return err
		}
	} else {
		return fmt.Errorf("key '%s' not found in ConfigMap '%s'", configMapKey, configMap.Name)
	}

	// Check if the node exists in the map
	_, exists := mappings[node]
	if !exists {
		log.Infof("Node '%s' does not exist in the ConfigMap, delete not required", node)
		return nil
	} else if mappings[node].PodNamespace != podNamespace {

		//Node-PodInfo mismatch, return error
		return fmt.Errorf("Node-podNamespace mismatch: node '%s' is occupied by pod '%s' in namespace '%s'. "+
			"Cannot delete by different namespace '%s'",
			node, mappings[node].PodName, mappings[node].PodNamespace, podNamespace)
	}
	// Delete the node from the map
	delete(mappings, node)

	updatedData, err := json.Marshal(mappings)
	if err != nil {
		return err
	}

	// Update the ConfigMap with the new JSON data
	configMap.Data[configMapKey] = string(updatedData)
	return nil
}

func retryConfigMapOperation(operation func(*v1.ConfigMap) error) error {
	var configMap *v1.ConfigMap
	var err error
	retryCount := 0
	maxRetries := 5

	for {
		configMap, err = loadConfigMap()
		if err != nil {
			return err
		}

		err = operation(configMap)
		if err != nil {
			return err
		}

		_, err = clientset.CoreV1().ConfigMaps(configMapNamespace).Update(context.TODO(), configMap, metav1.UpdateOptions{})
		if err == nil {
			log.Debugf("Successfully updated ConfigMap '%s'", configMapName)
			break
		}

		if errors.IsConflict(err) {
			retryCount++
			if retryCount >= maxRetries {
				log.Errorf("Max retries reached. Failed to update ConfigMap '%s' due to conflict.", configMapName)
				return err
			}
			log.Warnf("Conflict detected, retrying... (attempt %d/%d)", retryCount, maxRetries)
			time.Sleep(1 * time.Second)
			continue
		}

		log.Errorf("Failed to update ConfigMap: %v", err)
		return err
	}

	return nil
}

// Define getNamespaceLocks function from schedulerCoodirnator.go
func (c *ConfigMapSchedulerCoordinator) getNamespaceLocks(podNamespace string) map[string]bool {

	NodePodMap, err := getNodePodMapping()

	if err != nil {
		log.Fatalf("Error getting Node-Pod Locks: %s", err.Error())
	}
	var NodeList = map[string]bool{}
	for nodeName, podInfo := range NodePodMap {
		if podInfo.PodNamespace == podNamespace {
			NodeList[nodeName] = true
		}
	}

	return NodeList
}

// Define getAllNamespaceLocks function from schedulerCoodirnator.go
func (c *ConfigMapSchedulerCoordinator) getAllNamespacesLocks() map[string]bool {

	NodePodMap, err := getNodePodMapping()

	if err != nil {
		log.Fatalf("Error getting Node-Pod Locks: %s", err.Error())
	}
	var NodeList = map[string]bool{}
	for nodeName, _ := range NodePodMap {
		NodeList[nodeName] = true
	}

	return NodeList
}

// Define acquireLock function from schedulerCoodirnator.go
func (c *ConfigMapSchedulerCoordinator) acquireLock(nodeName string, podName string, podNamespace string) bool {

	err := updateNodePodMapping(nodeName, PodInfo{PodName: podName, PodNamespace: podNamespace})

	if err != nil {
		if strings.Contains(err.Error(), "Node already occupied") {
			log.Infof("Cannot acquire lock on node %s: %v", nodeName, err)
			return false
		} else {
			log.Fatalf("Failed to acquire lock on node %s: %v", nodeName, err)
		}
	}

	log.Infof("Acquired lock on node %s for pod %s in namespace %s", nodeName, podName, podNamespace)
	return true
}

// Define releaseLock function from schedulerCoodirnator.go
func (c *ConfigMapSchedulerCoordinator) releaseLock(nodeName string, podNamespace string) bool {

	err := deleteNodePodMapping(nodeName, podNamespace)

	if err != nil {
		if strings.Contains(err.Error(), "Node-podNamespace mismatch") {
			log.Infof("Cannot release lock aquired by another pod-namespace: %v", err)
			return false
		} else {
			log.Fatalf("Failed to release lock on node %s: %v", nodeName, err)
		}
	}

	log.Infof("Released lock on node %s by namespace %s", nodeName, podNamespace)
	return true
}
