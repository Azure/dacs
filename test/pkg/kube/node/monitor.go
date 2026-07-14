// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

package node

import (
	"context"
	"fmt"

	"github.com/go-logr/logr"
	corev1 "k8s.io/api/core/v1"
	clientcache "k8s.io/client-go/tools/cache"
	"sigs.k8s.io/controller-runtime/pkg/cache"
	"sigs.k8s.io/controller-runtime/pkg/client"
)

// AddMonitor registers event handlers on the controller-runtime cache to log
// node lifecycle events (add, delete, schedulable changes, ready transitions).
// It logs pod status on affected nodes across the specified namespaces.
func AddMonitor(ctx context.Context, c client.Client, runtimeCache cache.Cache, namespaces []string, log logr.Logger) error {
	informer, err := runtimeCache.GetInformer(ctx, &corev1.Node{})
	if err != nil {
		return fmt.Errorf("failed to get node informer: %w", err)
	}

	_, err = informer.AddEventHandler(clientcache.ResourceEventHandlerFuncs{
		AddFunc: func(obj interface{}) {
			n, ok := obj.(*corev1.Node)
			if !ok {
				return
			}
			status, reason := getReadyConditionStatus(n.Status.Conditions)
			log.Info("node added", "node", n.Name, "ready", status, "reason", reason)
			logPodStatus(ctx, c, n.Name, namespaces, log)
		},
		DeleteFunc: func(obj interface{}) {
			n, ok := obj.(*corev1.Node)
			if !ok {
				tombstone, tOk := obj.(clientcache.DeletedFinalStateUnknown)
				if !tOk {
					return
				}
				n, ok = tombstone.Obj.(*corev1.Node)
				if !ok {
					return
				}
			}
			log.Info("node deleted", "node", n.Name)
		},
		UpdateFunc: func(oldObj, newObj interface{}) {
			oldNode, ok := oldObj.(*corev1.Node)
			if !ok {
				return
			}
			newNode, ok := newObj.(*corev1.Node)
			if !ok {
				return
			}

			if oldNode.Spec.Unschedulable != newNode.Spec.Unschedulable {
				log.Info("node schedulable changed", "node", newNode.Name,
					"old", !oldNode.Spec.Unschedulable, "new", !newNode.Spec.Unschedulable)
				logPodStatus(ctx, c, newNode.Name, namespaces, log)
			}

			oldStatus, _ := getReadyConditionStatus(oldNode.Status.Conditions)
			newStatus, newReason := getReadyConditionStatus(newNode.Status.Conditions)
			if oldStatus != newStatus {
				log.Info("node ready changed", "node", newNode.Name,
					"old", oldStatus, "new", newStatus, "reason", newReason)
				logPodStatus(ctx, c, newNode.Name, namespaces, log)
			}
		},
	})

	return err
}

func getReadyConditionStatus(conditions []corev1.NodeCondition) (corev1.ConditionStatus, string) {
	for _, c := range conditions {
		if c.Type == corev1.NodeReady {
			return c.Status, c.Reason
		}
	}
	return corev1.ConditionUnknown, ""
}

func logPodStatus(ctx context.Context, c client.Client, nodeName string, namespaces []string, log logr.Logger) {
	for _, ns := range namespaces {
		var pods corev1.PodList
		if err := c.List(ctx, &pods,
			client.InNamespace(ns),
			client.MatchingFields{"spec.nodeName": nodeName},
		); err != nil {
			log.Info("failed to list pods on node", "node", nodeName, "namespace", ns, "error", err)
			continue
		}
		for i := range pods.Items {
			log.Info("pod status", "pod", fmt.Sprintf("%s/%s", ns, pods.Items[i].Name),
				"status", getPodStatus(&pods.Items[i]))
		}
	}
}

func getPodStatus(p *corev1.Pod) string {
	if p.DeletionTimestamp != nil {
		return "Terminating"
	}
	for _, cs := range p.Status.ContainerStatuses {
		if cs.State.Waiting != nil {
			return cs.State.Waiting.Reason
		}
	}
	return string(p.Status.Phase)
}
