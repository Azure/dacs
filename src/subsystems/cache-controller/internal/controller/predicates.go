/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package controller

import (
	"maps"

	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/event"
	"sigs.k8s.io/controller-runtime/pkg/predicate"
)

// cacheserverPodPredicate filters pod events to only those relevant to the
// custom scheduler. It accepts cacheserver pods (label app=cacheserver) and,
// for updates, only fires when a scheduling-relevant field changes:
//
//   - Phase (Pending->Running, etc.)
//   - NodeName (pod got scheduled or rescheduled)
//   - DeletionTimestamp (pod entering termination)
//   - Container readiness (any container Ready flag flipped)
type cacheserverPodPredicate struct {
	predicate.Funcs
}

const cacheserverAppLabelValue = "cacheserver"

func isCacheserverPod(obj client.Object) bool {
	return obj.GetLabels()["app"] == cacheserverAppLabelValue
}

func (cacheserverPodPredicate) Create(e event.CreateEvent) bool {
	return isCacheserverPod(e.Object)
}

func (cacheserverPodPredicate) Delete(e event.DeleteEvent) bool {
	return isCacheserverPod(e.Object)
}

func (cacheserverPodPredicate) Generic(e event.GenericEvent) bool {
	return isCacheserverPod(e.Object)
}

func (cacheserverPodPredicate) Update(e event.UpdateEvent) bool {
	if !isCacheserverPod(e.ObjectNew) {
		return false
	}

	oldPod, ok1 := e.ObjectOld.(*corev1.Pod)
	newPod, ok2 := e.ObjectNew.(*corev1.Pod)
	if !ok1 || !ok2 {
		return true
	}

	// Phase change (e.g. Pending -> Running, Running -> Failed)
	if oldPod.Status.Phase != newPod.Status.Phase {
		return true
	}

	// Pod got scheduled or moved to a different node
	if oldPod.Spec.NodeName != newPod.Spec.NodeName {
		return true
	}

	// Pod entering termination
	oldDeleting := oldPod.DeletionTimestamp != nil
	newDeleting := newPod.DeletionTimestamp != nil
	if oldDeleting != newDeleting {
		return true
	}

	// Container readiness changed
	if containerReadinessChanged(oldPod, newPod) {
		return true
	}

	return false
}

// containerReadinessChanged returns true if any container's Ready status
// differs between oldPod and newPod.
func containerReadinessChanged(oldPod, newPod *corev1.Pod) bool {
	oldReady := make(map[string]bool, len(oldPod.Status.ContainerStatuses))
	for _, cs := range oldPod.Status.ContainerStatuses {
		oldReady[cs.Name] = cs.Ready
	}
	newReady := make(map[string]bool, len(newPod.Status.ContainerStatuses))
	for _, cs := range newPod.Status.ContainerStatuses {
		newReady[cs.Name] = cs.Ready
	}
	return !maps.Equal(oldReady, newReady)
}

// stsStatusChangedPredicate fires on Update only when a StatefulSet's replica
// status changes (ReadyReplicas or Replicas). Create, Delete, and Generic
// events pass through unchanged via the embedded predicate.Funcs defaults.
type stsStatusChangedPredicate struct {
	predicate.Funcs
}

func (stsStatusChangedPredicate) Update(e event.UpdateEvent) bool {
	oldSts, ok1 := e.ObjectOld.(*appsv1.StatefulSet)
	newSts, ok2 := e.ObjectNew.(*appsv1.StatefulSet)
	if !ok1 || !ok2 {
		return false
	}
	return oldSts.Status.ReadyReplicas != newSts.Status.ReadyReplicas ||
		oldSts.Status.Replicas != newSts.Status.Replicas
}
