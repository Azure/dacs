/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package controller

import (
	"maps"

	corev1 "k8s.io/api/core/v1"
	"sigs.k8s.io/controller-runtime/pkg/event"
	"sigs.k8s.io/controller-runtime/pkg/predicate"
)

// NodePredicate filters Node events to only those that affect scaling
// decisions: create/delete events, label changes, and unschedulable changes.
type NodePredicate struct {
	predicate.Funcs
}

func (NodePredicate) Create(e event.CreateEvent) bool {
	return true
}

func (NodePredicate) Delete(e event.DeleteEvent) bool {
	return true
}

func (NodePredicate) Update(e event.UpdateEvent) bool {
	oldNode, ok1 := e.ObjectOld.(*corev1.Node)
	newNode, ok2 := e.ObjectNew.(*corev1.Node)
	if !ok1 || !ok2 {
		return false
	}
	// Only reconcile if labels changed (could affect selector match)
	// or unschedulable changed (affects schedulable node count).
	if !maps.Equal(oldNode.Labels, newNode.Labels) {
		return true
	}
	if oldNode.Spec.Unschedulable != newNode.Spec.Unschedulable {
		return true
	}

	// TODO(jlong): Taint changes are intentionally ignored because neither
	// the scaler nor the scheduler accounts for taints today. Add a taint
	// comparison here once taint-aware scheduling is implemented.

	return false
}

func (NodePredicate) Generic(e event.GenericEvent) bool {
	return false
}
