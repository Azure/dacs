/*
Copyright (c) Microsoft Corporation.
Licensed under the Apache License, Version 2.0.
*/

package scaling

import (
	"sync"
	"time"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/cache-controller/internal/config"
	"github.com/go-logr/logr"
	"k8s.io/apimachinery/pkg/api/equality"
	"k8s.io/apimachinery/pkg/types"
)

// minStabilizationRequeue is the minimum requeue duration when stabilization
// is holding back a scaling change. This covers the edge case where clock skew
// or rounding makes the computed remaining window duration zero or negative.
const minStabilizationRequeue = 1 * time.Second

// Clock abstracts time so the Normalizer can be tested without real waits.
type clock interface {
	Now() time.Time
}

// RealClock uses the system clock.
type realClock struct{}

func (realClock) Now() time.Time { return time.Now() }

// scalingState tracks per-CR normalizer state: the last-seen scaling spec and
// timestamps tracking when the desired replica count last crossed the current
// count in each direction.
type scalingState struct {
	lastAtOrAbove time.Time
	lastAtOrBelow time.Time
	lastSpec      *storagev1.DynamicScaleConfig
}

// NormalizationResult holds the output of NormalizeDesiredReplicas.
type normalizationResult struct {
	// Replicas is the replica count to apply after tolerance and stabilization.
	Replicas int32
	// RequeueAfter is non-zero when stabilization is holding back a change.
	// The controller should requeue after this duration so the change is
	// re-evaluated once the window expires.
	// Zero means no requeue is needed.
	RequeueAfter time.Duration
	// StabilizationEnds is the absolute time when the current stabilization
	// window expires. Zero value when no window is active.
	StabilizationEnds time.Time
}

// Normalizer applies HPA-inspired stabilization to raw replica proposals.
// It implements two stages:
//  1. Tolerance check -- suppresses scaling when the change is too small
//  2. Stabilization window -- blocks scaling until the desired count has been
//     continuously on one side of the current count for the full window
//     duration, then jumps directly to the desired value
//
// Unlike HPA, no rate limiting is applied -- all changes are applied at once
// since StatefulSet pod creation is expensive and should be batched.
//
// State is stored as two timestamps per Cache CR (O(1) memory) in a sync.Map.
// Each CR reconciles independently (keyed by namespace/name), so different CRs
// access different keys concurrently with no contention.
//
// TODO(jlong): stabilization state is lost on controller restart. Persist to
// CR status or annotation so the scaling controller does not lose memory of
// recent recommendations across restarts.
type normalizer struct {
	state  sync.Map // types.NamespacedName -> *scalingState
	clock  clock
	config *config.Config
}

// NewNormalizer creates a Normalizer with the system clock and the given config.
func newNormalizer(cfg *config.Config) *normalizer {
	return &normalizer{
		clock:  realClock{},
		config: cfg,
	}
}

// NormalizeDesiredReplicas applies tolerance and stabilization to the raw
// desired replica count computed by CalculateDesiredReplicas.
//
// The raw signal is always recorded for stabilization timestamps before
// tolerance is applied, so the stabilization window accumulates correctly
// even when tolerance suppresses the change.
//
// Tolerance suppresses scaling when the proportional change from current to
// desired is within the configured percentage band.
//
// Stabilization uses hold-and-jump semantics: scaling is blocked until the
// desired count has been continuously above (for scale-up) or below (for
// scale-down) the current count for the full stabilization window duration.
// When the window expires, the normalizer jumps directly to the latest desired
// value -- avoiding staircase scaling during monotonic ramps (e.g., AKS node
// pool scaling) which would trigger multiple expensive ring rebalances.
func (n *normalizer) NormalizeDesiredReplicas(key types.NamespacedName, currentReplicas, rawDesired int32, dynamic *storagev1.DynamicScaleConfig, log logr.Logger) (normalizationResult, error) {
	scaleUpTolerance := float64(n.config.ScaleUpTolerancePercent) / 100.0
	scaleDownTolerance := float64(n.config.ScaleDownTolerancePercent) / 100.0

	log.V(2).Info("NormalizeDesiredReplicas called",
		"currentReplicas", currentReplicas,
		"rawDesired", rawDesired,
		"scaleUpTolerance", scaleUpTolerance,
		"scaleDownTolerance", scaleDownTolerance)

	now := n.clock.Now()

	// Tolerance - suppress scaling if the proportional change is within
	// the tolerance band. This runs BEFORE recordObservation so that
	// tolerance-suppressed signals do not accumulate stabilization credit.
	if currentReplicas > 0 {
		ratio := float64(rawDesired) / float64(currentReplicas)
		if rawDesired > currentReplicas {
			if ratio <= 1.0+scaleUpTolerance {
				log.V(2).Info("Suppressing scale-up due to tolerance", "ratio", ratio, "tolerance", scaleUpTolerance)
				rawDesired = currentReplicas
			}
		} else if rawDesired < currentReplicas {
			if ratio >= 1.0-scaleDownTolerance {
				log.V(2).Info("Suppressing scale-down due to tolerance", "ratio", ratio, "tolerance", scaleDownTolerance)
				rawDesired = currentReplicas
			}
		}
	}

	// Record the (possibly tolerance-clamped) signal and check for spec
	// changes. If the spec changed, stabilization state is reset so the new
	// config takes effect immediately.
	n.recordObservation(key, rawDesired, currentReplicas, now, dynamic, log)
	return n.stabilize(key, rawDesired, currentReplicas, now, dynamic, log), nil
}

// recordObservation checks for spec changes and updates stabilization
// timestamps. If the scaling spec changed, stabilization state is purged so
// user-initiated changes take effect immediately. Timestamps are updated with
// the tolerance-clamped desired count so that tolerance-suppressed signals
// (clamped to currentReplicas) refresh both lastAtOrAbove and lastAtOrBelow,
// preventing stale timestamps from letting a later above-tolerance spike
// bypass stabilization.
func (n *normalizer) recordObservation(key types.NamespacedName, rawDesired, currentReplicas int32, now time.Time, dynamic *storagev1.DynamicScaleConfig, log logr.Logger) {
	val, ok := n.state.Load(key)
	if !ok {
		return
	}
	s := val.(*scalingState)

	// If the scaling spec changed, reset timestamps so the new config
	// takes effect immediately.
	if !equality.Semantic.DeepEqual(s.lastSpec, dynamic) {
		log.V(2).Info("Scaling spec changed, resetting stabilization state")
		s.lastSpec = dynamic.DeepCopy()
		s.lastAtOrAbove = time.Time{}
		s.lastAtOrBelow = time.Time{}
		return
	}

	if rawDesired >= currentReplicas {
		s.lastAtOrAbove = now
	}
	if rawDesired <= currentReplicas {
		s.lastAtOrBelow = now
	}
}

// stabilize determines whether scaling should proceed based on two timestamps
// tracking when the desired count last crossed the current replica count.
// Timestamps are updated by recordObservation before this is called.
//
// Scale-up is allowed when desired has been continuously above current for the
// full upWindow (i.e., lastAtOrBelow is older than upWindow).
// Scale-down is allowed when desired has been continuously below current for
// the full downWindow (i.e., lastAtOrAbove is older than downWindow).
func (n *normalizer) stabilize(key types.NamespacedName, desired, currentReplicas int32, now time.Time, dynamic *storagev1.DynamicScaleConfig, log logr.Logger) normalizationResult {
	upWindow := n.config.ScaleUpStabilizationWindow
	downWindow := n.config.ScaleDownStabilizationWindow
	log = log.WithValues("desired", desired)

	val, ok := n.state.Load(key)
	if !ok {
		// First observation -- create state and scale immediately. This also
		// handles controller restarts: in-memory state is lost so the first
		// reconciliation after restart scales to the desired value without
		// waiting for a stabilization window. The StatefulSet preserves the
		// last-applied replica count, preventing unnecessary churn.
		log.V(2).Info("First observation, scaling immediately")
		n.state.Store(key, &scalingState{
			lastAtOrAbove: now,
			lastAtOrBelow: now,
			lastSpec:      dynamic.DeepCopy(),
		})
		return normalizationResult{Replicas: desired}
	}
	s := val.(*scalingState)

	// Timestamps were zeroed by recordObservation after a spec change.
	// Initialize them and scale immediately.
	if s.lastAtOrAbove.IsZero() {
		log.V(2).Info("Spec change detected, scaling immediately")
		s.lastAtOrAbove = now
		s.lastAtOrBelow = now
		return normalizationResult{Replicas: desired}
	}

	result := desired
	var requeueAfter time.Duration
	var stabilizationEnds time.Time

	// Block scaling until desired has stayed on one side of current for the
	// full stabilization window.
	//
	// Scale-up case: desired > current. We check how long it has been since
	// desired was at or below current (lastAtOrBelow). If that was less than
	// upWindow ago, the desired count hasn't been continuously above current
	// long enough -- hold at currentReplicas and requeue for the remainder.
	//
	// Scale-down case: desired < current. Same logic in reverse -- we check
	// lastAtOrAbove against downWindow.
	//
	// When the window expires the hold is released and we jump straight to
	// the latest desired value, avoiding incremental staircase scaling.
	if desired > currentReplicas && upWindow > 0 {
		if age := now.Sub(s.lastAtOrBelow); age < upWindow {
			result = currentReplicas
			requeueAfter = upWindow - age
			stabilizationEnds = s.lastAtOrBelow.Add(upWindow)
			log.V(2).Info("Stabilization holding back scale-up", "requeueAfter", requeueAfter)
		}
	} else if desired < currentReplicas && downWindow > 0 {
		if age := now.Sub(s.lastAtOrAbove); age < downWindow {
			result = currentReplicas
			requeueAfter = downWindow - age
			stabilizationEnds = s.lastAtOrAbove.Add(downWindow)
			log.V(2).Info("Stabilization holding back scale-down", "requeueAfter", requeueAfter)
		}
	}

	// If we're holding back a change, ensure we always requeue. Due to clock
	// skew or rounding, the computed duration may be zero or negative.
	// Zero means "no requeue needed", but we know a requeue is needed to evaluate
	// when the window expires, so set it to a small positive value.
	if result != desired && requeueAfter <= 0 {
		requeueAfter = minStabilizationRequeue
	}

	log.V(2).Info("Normalization result", "result", result, "currentReplicas", currentReplicas, "requeueAfter", requeueAfter)

	return normalizationResult{
		Replicas:          result,
		RequeueAfter:      requeueAfter,
		StabilizationEnds: stabilizationEnds,
	}
}

// Purge removes all stored state for a given key.
// Called when a Cache CR switches from dynamic to static scaling or is deleted.
func (n *normalizer) Purge(key types.NamespacedName) {
	n.state.Delete(key)
}
