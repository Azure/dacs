/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package scaling

import (
	"context"
	"fmt"
	"time"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/cache-controller/internal/config"
	"github.com/Azure/dacs/cache-controller/internal/statushelper"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/types"
	"k8s.io/klog/v2"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
	logf "sigs.k8s.io/controller-runtime/pkg/log"
	"sigs.k8s.io/controller-runtime/pkg/reconcile"
)

const (
	cacheDynamicScalingField = "spec.scaling.dynamic"
	nodeUnschedulableField   = "spec.unschedulable"
)

// Reconciler provides dynamic scaling by computing desired replica counts
// based on node availability and cache configuration. It is embedded as a
// field in the CacheReconciler rather than running as a standalone controller.
type Reconciler struct {
	client.Client
	scheme     *runtime.Scheme
	calculator func(nodeCount int, percentageOfNodes int32, minServers, maxServers *int32) (int32, error)
	normalizer *normalizer
}

// New creates a new scaling Reconciler with the given dependencies.
func New(c client.Client, scheme *runtime.Scheme, cfg *config.Config) *Reconciler {
	return &Reconciler{
		Client:     c,
		scheme:     scheme,
		calculator: CalculateDesiredReplicas,
		normalizer: newNormalizer(cfg),
	}
}

// RegisterIndexes registers the field indexes needed for efficient node
// and cache lookups used during scaling calculations.
func (r *Reconciler) RegisterIndexes(ctx context.Context, mgr ctrl.Manager) error {
	if err := mgr.GetFieldIndexer().IndexField(ctx, &corev1.Node{}, nodeUnschedulableField, func(obj client.Object) []string {
		node := obj.(*corev1.Node)
		if node.Spec.Unschedulable {
			return []string{"true"}
		}
		return []string{"false"}
	}); err != nil {
		return fmt.Errorf("failed to register node unschedulable index: %w", err)
	}

	if err := mgr.GetFieldIndexer().IndexField(ctx, &storagev1.Cache{}, cacheDynamicScalingField, func(obj client.Object) []string {
		cache := obj.(*storagev1.Cache)
		if cache.Spec.Scaling.Dynamic != nil {
			return []string{"true"}
		}
		return []string{"false"}
	}); err != nil {
		return fmt.Errorf("failed to register cache dynamic scaling index: %w", err)
	}

	return nil
}

// Deleted cleans up any in-memory state associated with a Cache that has been
// deleted. Call this when the Cache resource is no longer found.
func (r *Reconciler) Deleted(key types.NamespacedName) {
	r.normalizer.Purge(key)
}

// DesiredReplicas computes the target replica count for a Cache based on its
// scaling configuration (static or dynamic). The ScalingStatus field is set on
// the statushelper. The desired count is returned.
func (r *Reconciler) DesiredReplicas(ctx context.Context, cache *storagev1.Cache, currentReplicas int32, h *statushelper.Helper) (int32, error) {
	switch {
	case cache.Spec.Scaling.Static != nil:
		r.normalizer.Purge(client.ObjectKeyFromObject(cache))
		h.SetScalingStatus(nil)
		return cache.Spec.Scaling.Static.NumServers, nil
	case cache.Spec.Scaling.Dynamic != nil:
		return r.desiredDynamicReplicas(ctx, cache, currentReplicas, h)
	default:
		h.SetScalingStatus(&storagev1.ScalingStatus{
			Message: "No scaling configuration found",
		})
		return 0, fmt.Errorf("cache %s/%s does not have scaling configuration", cache.Namespace, cache.Name)
	}
}

// desiredDynamicReplicas computes the target replica count for a dynamically
// scaled cache by listing schedulable nodes matching the cache's node selector,
// applying the calculator function, and then normalizing the result through
// tolerance and stabilization checks (modeled after the K8s HPA controller).
func (r *Reconciler) desiredDynamicReplicas(ctx context.Context, cache *storagev1.Cache, currentReplicas int32, h *statushelper.Helper) (int32, error) {
	log := logf.FromContext(ctx)

	var nodes corev1.NodeList
	listOpts := []client.ListOption{
		client.MatchingFields{nodeUnschedulableField: "false"},
	}
	if cache.Spec.NodeSelectorKey != nil && cache.Spec.NodeSelectorValue != nil {
		listOpts = append(listOpts, client.MatchingLabels{*cache.Spec.NodeSelectorKey: *cache.Spec.NodeSelectorValue})
	}
	if err := r.List(ctx, &nodes, listOpts...); err != nil {
		h.SetScalingStatus(&storagev1.ScalingStatus{
			Message: fmt.Sprintf("Failed to list nodes: %v", err),
		})
		return 0, fmt.Errorf("failed to list nodes: %w", err)
	}

	dynamic := cache.Spec.Scaling.Dynamic
	key := client.ObjectKeyFromObject(cache)

	nodeCount := len(nodes.Items)
	rawDesired, err := r.calculator(nodeCount, dynamic.PercentageOfNodesToUse, dynamic.MinServers, dynamic.MaxServers)
	if err != nil {
		h.SetScalingStatus(&storagev1.ScalingStatus{
			Message: fmt.Sprintf("Failed to calculate target servers: %v", err),
		})
		return 0, fmt.Errorf("failed to calculate desired replicas: %w", err)
	}

	log.V(1).Info("Scaling calculation", "matchingNodes", nodeCount,
		"percentage", dynamic.PercentageOfNodesToUse, "rawDesired", rawDesired, "currentReplicas", currentReplicas)

	norm, err := r.normalizer.NormalizeDesiredReplicas(key, currentReplicas, rawDesired, dynamic, log)
	if err != nil {
		return 0, err
	}

	if norm.RequeueAfter > 0 {
		log.V(1).Info("Stabilization holding back scaling", "rawDesired", rawDesired, "stabilized", norm.Replicas, "requeueAfter", norm.RequeueAfter.Round(time.Second))
		h.SetRequeueAfter(norm.RequeueAfter)
		ends := metav1.NewTime(norm.StabilizationEnds)
		h.SetScalingStatus(&storagev1.ScalingStatus{
			TargetServers:     &rawDesired,
			StabilizationEnds: &ends,
			Message:           fmt.Sprintf("Stabilization window active, scaling to %d servers after %s", rawDesired, ends.UTC().Format(time.RFC3339)),
		})
		return norm.Replicas, nil
	}

	h.SetScalingStatus(&storagev1.ScalingStatus{
		TargetServers: &rawDesired,
		Message:       fmt.Sprintf("Scaling settled at %d servers", rawDesired),
	})
	return norm.Replicas, nil
}

// MapNodeToCaches maps a Node event to the Cache CRs whose NodeSelectorKey/Value
// match the node's labels, so the controller is triggered for the correct Cache.
// This lists all Cache CRs (not just dynamic-scaling ones) because node changes
// also affect scheduling decisions for statically-scaled caches.
func (r *Reconciler) MapNodeToCaches(ctx context.Context, obj client.Object) []reconcile.Request {
	log := logf.FromContext(ctx).WithName("MapNodeToCaches").WithValues("node", klog.KObj(obj))
	node, ok := obj.(*corev1.Node)
	if !ok || node == nil {
		return nil
	}

	if node.Labels == nil {
		log.V(4).Info("node has no labels, skipping since it can't match anything")
		return nil
	}

	var cacheList storagev1.CacheList
	if err := r.List(ctx, &cacheList); err != nil {
		log.Error(err, "failed to list caches for node event")
		return nil
	}

	var requests []reconcile.Request //nolint:prealloc // length unknown due to selector filtering
	for _, cache := range cacheList.Items {
		log := log.WithValues("cache", klog.KObj(&cache))
		if cache.Spec.NodeSelectorKey == nil {
			log.V(4).Info("cache has no node selector key, skipping")
			continue
		}
		if cache.Spec.NodeSelectorValue == nil {
			log.V(4).Info("cache has no node selector value, skipping")
			continue
		}
		if node.Labels[*cache.Spec.NodeSelectorKey] != *cache.Spec.NodeSelectorValue {
			log.V(4).Info("node label does not match cache node selector")
			continue
		}
		requests = append(requests, reconcile.Request{
			NamespacedName: types.NamespacedName{
				Namespace: cache.Namespace,
				Name:      cache.Name,
			},
		})
	}
	return requests
}
