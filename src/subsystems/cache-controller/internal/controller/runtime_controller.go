/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package controller

import (
	"context"
	"fmt"
	"time"

	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/runtime"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/controller"
	"sigs.k8s.io/controller-runtime/pkg/handler"
	logf "sigs.k8s.io/controller-runtime/pkg/log"
	"sigs.k8s.io/controller-runtime/pkg/reconcile"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/cache-controller/internal/management"
)

// RuntimeReconciler tracks nodes, cache pods, cache CRDs etc. and performs custom scheduling, autoscaling and other cache management operations.
type RuntimeReconciler struct {
	client.Client
	Scheme    *runtime.Scheme
	Tracker   management.Tracker
}

// +kubebuilder:rbac:groups="",resources=nodes,verbs=get;list;watch
// +kubebuilder:rbac:groups="",resources=pods,verbs=get;list;watch

func (r *RuntimeReconciler) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
	log := logf.FromContext(ctx)
	log.V(1).Info("Runtime reconcile triggered", "trigger", req.NamespacedName)

	var (
		nodes []management.Node
		pods  []management.Pod
		err   error
	)

	// Fetch current nodes/pods from controller-runtime cache
	nodes, err = management.GetNodesFromCache(ctx, r.Client)
	if err != nil {
		log.Error(err, "Failed to get nodes from cache")
		return ctrl.Result{}, err
	}

	pods, err = management.GetPodsFromCache(ctx, r.Client)
	if err != nil {
		log.Error(err, "Failed to get pods from cache")
		return ctrl.Result{}, err
	}

	log.V(1).Info("Fetched runtime resources",
		"nodes", len(nodes),
		"pods", len(pods))

	// Fetch current Cache CRDs
	var cacheList storagev1.CacheList
	if err := r.List(ctx, &cacheList); err != nil {
		log.Error(err, "Failed to list Cache CRDs")
		return ctrl.Result{}, err
	}

	log.V(1).Info("Fetched runtime CRDs", "caches", len(cacheList.Items))

	// Update tracker
	r.Tracker.UpdateTracker(nodes, pods, cacheList.Items)

	// Scheduler run
	schedulerInstructions := management.Schedule(ctx, r.Tracker, r.Client)
	log.V(1).Info("Scheduler generated instructions", "count", len(schedulerInstructions))

	// Apply scheduler instructions
	executionErrors := management.ExecuteSchedulerInstructionsOnK8s(ctx, r.Client, schedulerInstructions)
	if len(executionErrors) > 0 {
		log.Info("Errors while executing scheduler instructions", "errors", executionErrors)
		return ctrl.Result{}, fmt.Errorf("scheduler execution errors occurred")
	}

	// Note: We are requeuing after a short delay if there are any stuck\potentially stuck pods,
	// so that they can be acted upon in a timely manner, instead of waiting for another event to trigger the reconcile.
	stuckStatePodCount := r.Tracker.GetPodCountInStuckStates()
	if stuckStatePodCount > 0 {
		log.Info("Stuck pods detected, requeuing reconcile after short delay", "stuckStatePodCount", stuckStatePodCount)
		return ctrl.Result{RequeueAfter: 2 * time.Minute}, nil
	}
	return ctrl.Result{}, nil
}

// SetupWithManager sets up the controller with the Manager.
func (r *RuntimeReconciler) SetupWithManager(mgr ctrl.Manager) error {
	return ctrl.NewControllerManagedBy(mgr).
		// Disallow parallel reconcilles as tracking\scheduling\autoscaling logic is not thread safe
		WithOptions(controller.Options{MaxConcurrentReconciles: 1}).
		// Watch Nodes
		Watches(&corev1.Node{}, &handler.EnqueueRequestForObject{}).
		// Watch Pods with cacheserver label
		Watches(&corev1.Pod{}, handler.EnqueueRequestsFromMapFunc(
			func(ctx context.Context, obj client.Object) []reconcile.Request {
				pod, ok := obj.(*corev1.Pod)
				if !ok {
					return nil
				}

				if pod.Labels != nil && pod.Labels["app"] == "cacheserver" {
					return []reconcile.Request{{NamespacedName: client.ObjectKeyFromObject(obj)}}
				}
				return nil
			})).
		// Watch Cache CRDs
		Watches(&storagev1.Cache{}, &handler.EnqueueRequestForObject{}).
		Named("runtime").
		Complete(r)
}
