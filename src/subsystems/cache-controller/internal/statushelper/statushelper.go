/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

// Package statushelper provides a helper for accumulating Cache status
// conditions, requeue timing, and persisting them as a single merge-patch.
// Create one with New early in Reconcile, pass it to sub-reconcilers so they
// can call SetCondition / SetRequeueAfter, then call Helper.Patch to persist
// all accumulated changes.
package statushelper

import (
	"context"
	"errors"
	"fmt"
	"time"

	"k8s.io/apimachinery/pkg/api/meta"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
)

// Helper captures a pre-mutation snapshot of a Cache object and provides
// methods for mutating its status conditions. A deferred call to Patch
// sends only the fields that were actually changed.
type Helper struct {
	cache           *storagev1.Cache
	patchBase       client.Patch
	writer          client.SubResourceWriter
	desiredReplicas *int32
	requeueAfter    time.Duration
}

// New snapshots the current cache status so that a subsequent call to Patch
// sends only the fields that were actually mutated. All sub-conditions are
// initialized to Unknown so that SetReady produces a correct aggregate even
// if a sub-reconciler never runs (e.g. due to an early error).
func New(cache *storagev1.Cache, c client.Client) (*Helper, error) {
	if cache == nil {
		return nil, errors.New("statushelper: cache must not be nil")
	}
	if c == nil {
		return nil, errors.New("statushelper: client must not be nil")
	}
	h := &Helper{
		cache:     cache,
		patchBase: client.MergeFrom(cache.DeepCopy()),
		writer:    c.Status(),
	}
	for ct := range subConditions {
		h.MarkUnknown(ct, storagev1.ReasonNotEvaluated, "Condition has not been evaluated this reconcile")
	}
	return h, nil
}

// subConditions is the set of condition types that sub-reconcilers are
// responsible for. New() initialises each to Unknown; sub-reconcilers
// overwrite with the actual state as they run.
var subConditions = map[string]struct{}{
	storagev1.PodsScheduledCondition: {},
	storagev1.ReplicasReadyCondition: {},
}

// SetCondition sets or updates a condition on the Cache status.
// Delegates to meta.SetStatusCondition which handles LastTransitionTime correctly.
func (h *Helper) SetCondition(condition metav1.Condition) {
	meta.SetStatusCondition(&h.cache.Status.Conditions, condition)
}

// MarkTrue sets Status=True for the given condition type.
func (h *Helper) MarkTrue(conditionType, reason, messageFormat string, messageArgs ...interface{}) {
	h.mark(conditionType, metav1.ConditionTrue, reason, messageFormat, messageArgs...)
}

// MarkFalse sets Status=False for the given condition type.
func (h *Helper) MarkFalse(conditionType, reason, messageFormat string, messageArgs ...interface{}) {
	h.mark(conditionType, metav1.ConditionFalse, reason, messageFormat, messageArgs...)
}

// MarkUnknown sets Status=Unknown for the given condition type.
func (h *Helper) MarkUnknown(conditionType, reason, messageFormat string, messageArgs ...interface{}) {
	h.mark(conditionType, metav1.ConditionUnknown, reason, messageFormat, messageArgs...)
}

func (h *Helper) mark(conditionType string, status metav1.ConditionStatus, reason, messageFormat string, messageArgs ...interface{}) {
	h.SetCondition(metav1.Condition{
		Type:    conditionType,
		Status:  status,
		Reason:  reason,
		Message: fmt.Sprintf(messageFormat, messageArgs...),
	})
}

// SetDesiredReplicas records the desired replica count resolved during this
// reconciliation. Call this after resolveNumServers succeeds so that
// setFinalConditions can populate the Cache status fields.
func (h *Helper) SetDesiredReplicas(replicas int32) {
	h.desiredReplicas = &replicas
}

// DesiredReplicas returns the desired replica count if set, or nil.
func (h *Helper) DesiredReplicas() *int32 {
	return h.desiredReplicas
}

// SetRequeueAfter records a requeue duration. If called multiple times, the
// shortest positive duration wins so the controller re-evaluates at the
// earliest deadline.
func (h *Helper) SetRequeueAfter(d time.Duration) {
	if d > 0 && (h.requeueAfter == 0 || d < h.requeueAfter) {
		h.requeueAfter = d
	}
}

// RequeueAfter returns the shortest positive requeue duration, or zero.
func (h *Helper) RequeueAfter() time.Duration {
	return h.requeueAfter
}

// ToCtrlResult converts the accumulated result into a controller-runtime Result.
// Returns a zero Result when no requeue is needed.
func (h *Helper) ToCtrlResult() ctrl.Result {
	if h.requeueAfter > 0 {
		return ctrl.Result{RequeueAfter: h.requeueAfter}
	}
	return ctrl.Result{}
}

// SetReady computes the aggregate Ready condition from the known
// sub-conditions. Ready is True only when every sub-condition is True;
// otherwise False with the first non-True condition's reason and message.
func (h *Helper) SetReady() {
	for i := range h.cache.Status.Conditions {
		c := &h.cache.Status.Conditions[i]
		if _, ok := subConditions[c.Type]; !ok {
			continue
		}
		if c.Status != metav1.ConditionTrue {
			h.SetCondition(metav1.Condition{
				Type:    storagev1.ReadyCondition,
				Status:  metav1.ConditionFalse,
				Reason:  c.Reason,
				Message: c.Message,
			})
			return
		}
	}
	h.MarkTrue(storagev1.ReadyCondition, storagev1.ReasonAllConditionsMet, "Cache is ready")
}

// SetScalingStatus sets the ScalingStatus field on the Cache status.
// Pass nil to clear the field (e.g. for static scaling or error paths).
func (h *Helper) SetScalingStatus(s *storagev1.ScalingStatus) {
	h.cache.Status.Scaling = s
}

// Patch persists any status mutations made since the helper was created.
func (h *Helper) Patch(ctx context.Context) error {
	return h.writer.Patch(ctx, h.cache, h.patchBase)
}
