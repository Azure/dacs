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
	"k8s.io/client-go/kubernetes"
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
	Clientset kubernetes.Interface
	Tracker   management.Tracker
}

// +kubebuilder:rbac:groups="",resources=nodes,verbs=get;list;watch
// +kubebuilder:rbac:groups="",resources=pods,verbs=get;list;watch

func (r *RuntimeReconciler) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
	log := logf.FromContext(ctx)
	log.V(1).Info("Runtime reconcile triggered", "trigger", req.NamespacedName)

	// Fetch current nodes
	nodes, err := management.GetNodesFromK8s(r.Clientset)
	if err != nil {
		log.Error(err, "Failed to get nodes")
		return ctrl.Result{}, err
	}

	// Fetch current pods
	pods, err := management.GetPodsFromK8s(r.Clientset)
	if err != nil {
		log.Error(err, "Failed to get pods")
		return ctrl.Result{}, err
	}

	// Fetch current Cache CRDs
	var cacheList storagev1.CacheList
	if err := r.List(ctx, &cacheList); err != nil {
		log.Error(err, "Failed to list Cache CRDs")
		return ctrl.Result{}, err
	}

	// Update tracker
	r.Tracker.UpdateTracker(nodes, pods, cacheList.Items)

	// Scheduler run
	schedulerInstructions := management.Schedule(r.Tracker, r.Clientset)

	// Apply scheduler instructions
	executionErrors := management.ExecuteSchedulerInstructionsOnK8s(r.Clientset, schedulerInstructions)
	if len(executionErrors) > 0 {
		log.Info("Errors while executing scheduler instructions", "errors", executionErrors)
		return ctrl.Result{}, fmt.Errorf("scheduler execution errors occurred")
	}

	// Note: We are requeuing after a short delay to allow any stuck pods to be acted upon in a timely manner,
	// instead of waiting for another event to trigger the reconcile.
	return ctrl.Result{RequeueAfter: 2 * time.Minute}, nil
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
