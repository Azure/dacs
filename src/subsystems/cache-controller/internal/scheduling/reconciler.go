// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Package scheduling provides pod scheduling reconciliation for cache server
// pods. It wraps the management.Tracker and management.Schedule primitives
// into a Reconciler that follows the same sub-reconciler pattern as
// scaling.Reconciler: accept a statushelper, set conditions and requeue
// timing, and return.
//
// The scheduling primitives themselves (node selection, instruction generation)
// live in the management package. This package is responsible only for the
// reconciliation lifecycle around those primitives.
package scheduling

import (
	"context"
	"errors"
	"fmt"
	"time"

	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/types"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
	logf "sigs.k8s.io/controller-runtime/pkg/log"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/cache-controller/internal/management"
	"github.com/Azure/dacs/cache-controller/internal/statushelper"
)

const stuckPodRequeueInterval = 2 * time.Minute

// Reconciler handles custom pod scheduling for cache server pods. It
// tracks node/pod state, generates scheduling instructions (bind, delete),
// and executes them against the cluster.
type Reconciler struct {
	client.Client
	Tracker *management.Tracker
}

// NewReconciler creates a scheduling Reconciler.
func NewReconciler(c client.Client, tracker *management.Tracker) (*Reconciler, error) {
	if c == nil {
		return nil, fmt.Errorf("client must not be nil")
	}
	if tracker == nil {
		return nil, fmt.Errorf("tracker must not be nil")
	}
	return &Reconciler{Client: c, Tracker: tracker}, nil
}

// Reconcile runs one scheduling cycle for the given Cache CR. It fetches
// nodes and pods owned by the Cache's StatefulSet, updates the tracker,
// generates + executes scheduling instructions, and sets the
// PodsScheduled condition on the statushelper.
func (r *Reconciler) Reconcile(ctx context.Context, cache *storagev1.Cache, h *statushelper.Helper) error {
	if err := ctx.Err(); err != nil {
		return err
	}
	log := logf.FromContext(ctx)

	nodes, err := management.GetNodesFromCache(ctx, r.Client)
	if err != nil {
		h.MarkUnknown(storagev1.PodsScheduledCondition, storagev1.ReasonNodeListError, "Failed to list nodes: %v", err)
		return fmt.Errorf("failed to get nodes: %w", err)
	}

	pods, err := r.getOwnedPods(ctx, cache)
	if err != nil {
		h.MarkUnknown(storagev1.PodsScheduledCondition, storagev1.ReasonPodListError, "Failed to list pods: %v", err)
		return fmt.Errorf("failed to get pods: %w", err)
	}

	cacheKey := cache.Namespace + "/" + cache.Name

	// UpdateAndSchedule atomically updates tracker state and generates
	// scheduling instructions while holding the Tracker mutex. This
	// ensures concurrent reconciles see consistent global state. The lock
	// is released before executing instructions against the API server.
	instructions := r.Tracker.UpdateAndSchedule(ctx, cacheKey, nodes, pods)

	log.V(1).Info("Scheduler generated instructions", "count", len(instructions))

	executionErrors := management.ExecuteSchedulerInstructionsOnK8s(ctx, r.Client, instructions)
	if len(executionErrors) > 0 {
		log.Info("Errors executing scheduler instructions", "errors", executionErrors)
		h.MarkFalse(storagev1.PodsScheduledCondition, storagev1.ReasonSchedulingError,
			"Failed to execute %d scheduling instruction(s)", len(executionErrors))
		return fmt.Errorf("scheduler execution errors: %w", errors.Join(executionErrors...))
	}

	if stuckCount := r.Tracker.GetPodCountInStuckStates(cacheKey); stuckCount > 0 {
		log.Info("Stuck pods detected, will requeue", "stuckPodCount", stuckCount)
		h.MarkFalse(storagev1.PodsScheduledCondition, storagev1.ReasonPodsStuck,
			"%d pod(s) stuck in unschedulable or unhealthy state", stuckCount)
		h.SetRequeueAfter(stuckPodRequeueInterval)
		return nil
	}

	if waitingCount := r.Tracker.CountPodsAwaitingNodeRecovery(cacheKey); waitingCount > 0 {
		log.Info("Waiting pods detected due to short-term node loss", "waitingPodCount", waitingCount)
		h.MarkFalse(storagev1.PodsScheduledCondition, storagev1.ReasonPodsWaitingForNodeRecovery,
			"%d pod(s) waiting for short-term node loss recovery", waitingCount)
		// Requeue so that once the short-term loss window expires (and the node
		// is still unhealthy) the pod is moved to a healthy node. Node recovery
		// itself is picked up by the Node watch.
		h.SetRequeueAfter(stuckPodRequeueInterval)
		return nil
	}

	h.MarkTrue(storagev1.PodsScheduledCondition, storagev1.ReasonAllPodsScheduled, "All cache server pods are scheduled")
	return nil
}

// getOwnedPods returns the management.Pod list for pods that belong to this
// Cache CR, identified by the dacs.azure.com/cache-name label set on
// the StatefulSet pod template.
func (r *Reconciler) getOwnedPods(ctx context.Context, cache *storagev1.Cache) ([]management.Pod, error) {
	var podList corev1.PodList
	if err := r.List(ctx, &podList,
		client.InNamespace(cache.Namespace),
		client.MatchingLabels{
			management.PodLabelSelectorKey: management.PodLabelSelectorValue,
			management.CacheNameLabelKey:   cache.Name,
		},
	); err != nil {
		return nil, fmt.Errorf("failed to list pods: %w", err)
	}

	return management.BuildPodList(podList.Items), nil
}

// MapPodToCaches maps a pod event to the Cache CR that owns it by reading
// the dacs.azure.com/cache-name label from the pod. This avoids an API
// call to list all Caches in the namespace.
func (r *Reconciler) MapPodToCaches(_ context.Context, obj client.Object) []ctrl.Request {
	cacheName := obj.GetLabels()[management.CacheNameLabelKey]
	if cacheName == "" {
		return nil
	}
	return []ctrl.Request{
		{NamespacedName: types.NamespacedName{
			Name:      cacheName,
			Namespace: obj.GetNamespace(),
		}},
	}
}
