// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

package main

import (
	"context"
	"encoding/json"
	"fmt"
	"testing"

	"gotest.tools/v3/assert"
	v1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/kubernetes/fake"
)

// CreateConfigMap creates a ConfigMap with the given node-PodInfo mappings.
func CreateConfigMap(clientset kubernetes.Interface, namespace, configMapName string, nodePodMappings map[string]PodInfo) (*v1.ConfigMap, error) {
	mappingsJSON, err := json.Marshal(nodePodMappings)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal mappings: %v", err)
	}

	configMap := &v1.ConfigMap{
		ObjectMeta: metav1.ObjectMeta{
			Name:      configMapName,
			Namespace: namespace,
		},
		Data: map[string]string{
			"NodeCacheServerMapping": string(mappingsJSON),
		},
	}

	createdConfigMap, err := clientset.CoreV1().ConfigMaps(namespace).Create(context.TODO(), configMap, metav1.CreateOptions{})
	if err != nil {
		return nil, fmt.Errorf("failed to create ConfigMap: %v", err)
	}

	return createdConfigMap, nil
}

func TestSchedulerCoordinatorGetNamespaceLocks(t *testing.T) {
	// Create a fake clientset
	clientset := fake.NewSimpleClientset()
	mockLazyInit(clientset)

	// Create a fake ConfigMap
	nodePodMappings := map[string]PodInfo{
		"node1": PodInfo{
			PodName:      "cacheserver-1",
			PodNamespace: "azureml",
		},
		"node2": PodInfo{
			PodName:      "cacheserver-2",
			PodNamespace: "azureml",
		},
		"node3": PodInfo{
			PodName:      "cacheserver-1",
			PodNamespace: "cache-server",
		},
	}
	_, err := CreateConfigMap(clientset, "default", "node-cacheserver-mapping", nodePodMappings)
	if err != nil {
		t.Fatalf("Failed to create ConfigMap: %v", err)
	}

	coordinator := InitConfigMapSchedulerCoordinator()

	// Get the locks for the pod
	NodelocksAzureml := coordinator.getNamespaceLocks("azureml")

	// Check if the locks are correct
	expectedLocks := map[string]bool{"node1": true, "node2": true}
	assert.DeepEqual(t, NodelocksAzureml, expectedLocks)
}

func TestSchedulerCoordinatorGetAllNamespacesLocks(t *testing.T) {
	// Create a fake clientset
	clientset := fake.NewSimpleClientset()
	mockLazyInit(clientset)

	// Create a fake ConfigMap
	nodePodMappings := map[string]PodInfo{
		"node1": PodInfo{
			PodName:      "cacheserver-1",
			PodNamespace: "azureml",
		},
		"node2": PodInfo{
			PodName:      "cacheserver-2",
			PodNamespace: "azureml",
		},
		"node3": PodInfo{
			PodName:      "cacheserver-1",
			PodNamespace: "cache-server",
		},
	}
	_, err := CreateConfigMap(clientset, "default", "node-cacheserver-mapping", nodePodMappings)
	if err != nil {
		t.Fatalf("Failed to create ConfigMap: %v", err)
	}

	coordinator := InitConfigMapSchedulerCoordinator()

	// Get all the locks
	AllNamespacesLocks := coordinator.getAllNamespacesLocks()

	// Check if the locks are correct
	expectedLocks := map[string]bool{"node1": true, "node2": true, "node3": true}
	assert.DeepEqual(t, AllNamespacesLocks, expectedLocks)
}

func TestSchedulerCoordinatorAcquireLock(t *testing.T) {
	// Create a fake clientset
	clientset := fake.NewSimpleClientset()
	mockLazyInit(clientset)

	// Create a fake ConfigMap
	nodePodMappings := map[string]PodInfo{
		"node2": PodInfo{
			PodName:      "cacheserver-2",
			PodNamespace: "azureml",
		},
		"node3": PodInfo{
			PodName:      "cacheserver-1",
			PodNamespace: "cache-server",
		},
	}
	_, err := CreateConfigMap(clientset, "default", "node-cacheserver-mapping", nodePodMappings)
	if err != nil {
		t.Fatalf("Failed to create ConfigMap: %v", err)
	}

	coordinator := InitConfigMapSchedulerCoordinator()

	// Attempt to acquire a lock on already occupied node by other namespace
	acquired := coordinator.acquireLock("node3", "cacheserver-3", "azureml")
	assert.Equal(t, acquired, false)

	// Attempt to acquire a lock on already occupied node by same namespace
	acquired = coordinator.acquireLock("node2", "cacheserver-3", "azureml")
	assert.Equal(t, acquired, false)

	// Attempt to acquire a lock on unoccupied node
	NodeLocksAzureml := coordinator.getNamespaceLocks("azureml")
	_, ok := NodeLocksAzureml["node4"]
	assert.Equal(t, ok, false)
	acquired = coordinator.acquireLock("node4", "cacheserver-3", "azureml")
	assert.Equal(t, acquired, true)
	// Check if the lock is acquired, should have node4 in the locks
	NodeLocksAzureml = coordinator.getNamespaceLocks("azureml")
	_, ok = NodeLocksAzureml["node4"]
	assert.Equal(t, ok, true)
}

func TestSchedulerCoordinatorReleaseLock(t *testing.T) {
	// Create a fake clientset
	clientset := fake.NewSimpleClientset()
	mockLazyInit(clientset)

	// Create a fake ConfigMap
	nodePodMappings := map[string]PodInfo{
		"node2": PodInfo{
			PodName:      "cacheserver-2",
			PodNamespace: "azureml",
		},
		"node3": PodInfo{
			PodName:      "cacheserver-1",
			PodNamespace: "cache-server",
		},
	}
	_, err := CreateConfigMap(clientset, "default", "node-cacheserver-mapping", nodePodMappings)
	if err != nil {
		t.Fatalf("Failed to create ConfigMap: %v", err)
	}

	coordinator := InitConfigMapSchedulerCoordinator()

	// Attempt to release a lock on an occupied node by same namespace
	NodelocksAzureml := coordinator.getNamespaceLocks("azureml")
	_, ok := NodelocksAzureml["node2"]
	assert.Equal(t, ok, true)
	released := coordinator.releaseLock("node2", "azureml")
	assert.Equal(t, released, true)
	// Check if the lock is released, should not have node2 in the locks
	NodelocksAzureml = coordinator.getNamespaceLocks("azureml")
	_, ok = NodelocksAzureml["node2"]
	assert.Equal(t, ok, false)

	// Attempt to release a lock on a node that is occupied by another namespace
	released = coordinator.releaseLock("node3", "azureml")
	assert.Equal(t, released, false)

	// Attempt to release a lock on an unoccupied node
	// should be true as node is not locked
	released = coordinator.releaseLock("node4", "azureml")
	assert.Equal(t, released, true)
}
