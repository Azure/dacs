/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package scaling

import (
	"testing"
	"time"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/cache-controller/internal/config"
	"github.com/go-logr/logr"
	"github.com/go-logr/logr/testr"
	"k8s.io/apimachinery/pkg/types"
)

// FakeClock is a Clock implementation for tests with controllable time.
type FakeClock struct {
	T time.Time
}

func (c *FakeClock) Now() time.Time          { return c.T }
func (c *FakeClock) Advance(d time.Duration) { c.T = c.T.Add(d) }

var testKey = types.NamespacedName{Namespace: "test", Name: "cache"}

// newTestLogger returns a verbose test logger that surfaces V(2) debug output
// when running with go test -v.
func newTestLogger(t *testing.T) logr.Logger {
	return testr.NewWithOptions(t, testr.Options{Verbosity: 2})
}

// zeroScalingConfig returns a Config with no tolerance or stabilization,
// useful as a base for tests that configure specific values.
func zeroScalingConfig() *config.Config {
	return &config.Config{}
}

// defaultScalingConfig returns a Config matching operator defaults.
func defaultScalingConfig() *config.Config {
	return &config.Config{
		ScaleUpStabilizationWindow:   config.DefaultScaleUpStabilizationWindow,
		ScaleDownStabilizationWindow: config.DefaultScaleDownStabilizationWindow,
		ScaleUpTolerancePercent:      config.DefaultScaleUpTolerancePercent,
		ScaleDownTolerancePercent:    config.DefaultScaleDownTolerancePercent,
	}
}

func newTestNormalizer(clock *FakeClock, cfg *config.Config) *normalizer {
	return &normalizer{clock: clock, config: cfg}
}

// simpleDynamic returns a DynamicScaleConfig with only the percentage set.
func simpleDynamic() *storagev1.DynamicScaleConfig {
	return &storagev1.DynamicScaleConfig{
		PercentageOfNodesToUse: 50,
	}
}

func TestNormalizeDesiredReplicas_Tolerance(t *testing.T) {
	log := newTestLogger(t)
	t.Parallel()

	tests := []struct {
		name            string
		currentReplicas int32
		rawDesired      int32
		cfg             *config.Config
		expected        int32
	}{
		{
			name:            "scale-up within 10% tolerance is suppressed",
			currentReplicas: 10,
			rawDesired:      11,
			expected:        10,
		},
		{
			name:            "scale-up beyond 10% tolerance is allowed",
			currentReplicas: 10,
			rawDesired:      12,
			expected:        12,
		},
		{
			name:            "scale-down within 10% tolerance is suppressed",
			currentReplicas: 10,
			rawDesired:      9,
			expected:        10,
		},
		{
			name:            "scale-down beyond 10% tolerance is allowed",
			currentReplicas: 10,
			rawDesired:      8,
			expected:        8,
		},
		{
			name:            "no change returns current",
			currentReplicas: 5,
			rawDesired:      5,
			expected:        5,
		},
		{
			name:            "small cluster: 1 to 2 is 100% change, allowed",
			currentReplicas: 1,
			rawDesired:      2,
			expected:        2,
		},
		{
			name:            "custom tolerance 20%: 10 to 12 is within band",
			currentReplicas: 10,
			rawDesired:      12,
			cfg: &config.Config{
				ScaleUpTolerancePercent:   20,
				ScaleDownTolerancePercent: 20,
			},
			expected: 10,
		},
		{
			name:            "custom tolerance 0%: any change triggers scaling",
			currentReplicas: 10,
			rawDesired:      11,
			cfg:             zeroScalingConfig(),
			expected:        11,
		},
		{
			name:            "currentReplicas=0 returns raw desired (initial scale-up)",
			currentReplicas: 0,
			rawDesired:      5,
			expected:        5,
		},
		{
			name:            "asymmetric tolerance: strict up, loose down",
			currentReplicas: 10,
			rawDesired:      9,
			cfg: &config.Config{
				ScaleUpTolerancePercent:   5,
				ScaleDownTolerancePercent: 20,
			},
			expected: 10,
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			t.Parallel()
			clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}

			// Default: 10% tolerance with no stabilization windows.
			cfg := tc.cfg
			if cfg == nil {
				cfg = &config.Config{
					ScaleUpTolerancePercent:   10,
					ScaleDownTolerancePercent: 10,
				}
			}
			n := newTestNormalizer(clock, cfg)
			dynamic := simpleDynamic()

			got, err := n.NormalizeDesiredReplicas(testKey, tc.currentReplicas, tc.rawDesired, dynamic, log)
			if err != nil {
				t.Fatal(err)
			}
			if got.Replicas != tc.expected {
				t.Errorf("NormalizeDesiredReplicas(current=%d, raw=%d) = %d, want %d",
					tc.currentReplicas, tc.rawDesired, got.Replicas, tc.expected)
			}
		})
	}
}

func TestNormalizeDesiredReplicas_StabilizationWindow(t *testing.T) {
	log := newTestLogger(t)
	t.Parallel()

	t.Run("scale-down uses highest recommendation in window", func(t *testing.T) {
		t.Parallel()
		clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
		cfg := &config.Config{ScaleDownStabilizationWindow: 5 * time.Minute}
		n := newTestNormalizer(clock, cfg)
		b := simpleDynamic()

		// Seed at current level (as the controller does on first reconciliation)
		if _, err := n.NormalizeDesiredReplicas(testKey, 10, 10, b, log); err != nil {
			t.Fatal(err)
		}

		// T0+1s: recommend 8 (current=10) -> held by seed (10@T0 >= 10)
		clock.Advance(1 * time.Second)
		got, err := n.NormalizeDesiredReplicas(testKey, 10, 8, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 10 {
			t.Errorf("first scale-down: got %d, want 10 (held)", got.Replicas)
		}

		// T0+1m: recommend 6 -> still held (10@T0 still in window)
		clock.Advance(1 * time.Minute)
		got, err = n.NormalizeDesiredReplicas(testKey, 10, 6, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 10 {
			t.Errorf("second scale-down: got %d, want 10 (held)", got.Replicas)
		}
		if got.RequeueAfter == 0 {
			t.Error("second call: expected non-zero RequeueAfter")
		}

		// T0+2m: recommend 5 -> still held (10@T0 in window)
		clock.Advance(1 * time.Minute)
		got, err = n.NormalizeDesiredReplicas(testKey, 10, 5, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 10 {
			t.Errorf("third call: got %d, want 10 (held)", got.Replicas)
		}

		// T0+5m1s: seed (10@T0) expired. No recs >= 10 remain -> jump to desired.
		clock.Advance(3*time.Minute + 1*time.Second)
		got, err = n.NormalizeDesiredReplicas(testKey, 10, 5, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 5 {
			t.Errorf("after window: got %d, want 5 (jump to desired)", got.Replicas)
		}
		if got.RequeueAfter != 0 {
			t.Errorf("after window: RequeueAfter=%v, want 0", got.RequeueAfter)
		}
	})

	t.Run("scale-up holds until baseline expires then jumps to desired", func(t *testing.T) {
		t.Parallel()
		clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
		cfg := &config.Config{ScaleUpStabilizationWindow: 60 * time.Second}
		n := newTestNormalizer(clock, cfg)
		b := simpleDynamic()

		// T0: seed at current level
		got, err := n.NormalizeDesiredReplicas(testKey, 10, 10, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 10 {
			t.Errorf("seed: got %d, want 10", got.Replicas)
		}

		// T0+10s: recommend 15 -> held because 10@T0 is in window (<= current)
		clock.Advance(10 * time.Second)
		got, err = n.NormalizeDesiredReplicas(testKey, 10, 15, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 10 {
			t.Errorf("first scale-up: got %d, want 10 (held by window)", got.Replicas)
		}
		if got.RequeueAfter == 0 {
			t.Error("first scale-up: expected non-zero RequeueAfter")
		}

		// T0+20s: recommend 20 -> still held (10@T0 still in window)
		clock.Advance(10 * time.Second)
		got, err = n.NormalizeDesiredReplicas(testKey, 10, 20, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 10 {
			t.Errorf("second scale-up: got %d, want 10 (still held)", got.Replicas)
		}

		// T0+61s: 10@T0 expired -> jump straight to desired (20), not to
		// intermediate (15). Single scale operation.
		clock.Advance(41 * time.Second)
		got, err = n.NormalizeDesiredReplicas(testKey, 10, 20, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 20 {
			t.Errorf("after window: got %d, want 20 (jump to desired)", got.Replicas)
		}
	})

	t.Run("default config uses 15 min stabilization", func(t *testing.T) {
		t.Parallel()
		clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
		n := newTestNormalizer(clock, defaultScalingConfig())
		b := simpleDynamic()

		// Seed at current level
		if _, err := n.NormalizeDesiredReplicas(testKey, 20, 20, b, log); err != nil {
			t.Fatal(err)
		}

		// T0+1s: current=20, raw=14 -> 30% drop exceeds tolerance -> passes to stabilization
		// Held by seed (20@T0 >= 20)
		clock.Advance(1 * time.Second)
		got, err := n.NormalizeDesiredReplicas(testKey, 20, 14, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 20 {
			t.Errorf("first scale-down: got %d, want 20 (held)", got.Replicas)
		}

		// T0+1m: raw=10, 50% drop from 20, passes tolerance. Still held.
		clock.Advance(1 * time.Minute)
		got, err = n.NormalizeDesiredReplicas(testKey, 20, 10, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 20 {
			t.Errorf("second call: got %d, want 20 (stabilization holds)", got.Replicas)
		}

		// T0+15m1s: seed (20@T0) expired -> jump to desired
		clock.Advance(14*time.Minute + 1*time.Second)
		got, err = n.NormalizeDesiredReplicas(testKey, 20, 10, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 10 {
			t.Errorf("after window: got %d, want 10", got.Replicas)
		}
	})

	t.Run("RequeueAfter is set correctly", func(t *testing.T) {
		t.Parallel()
		clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
		cfg := &config.Config{ScaleDownStabilizationWindow: 2 * time.Minute}
		n := newTestNormalizer(clock, cfg)
		b := simpleDynamic()

		// Seed at current level
		if _, err := n.NormalizeDesiredReplicas(testKey, 10, 10, b, log); err != nil {
			t.Fatal(err)
		}

		// T0+30s: recommend 5 -> held by seed (10@T0 >= 10)
		// RequeueAfter = (T0 + 120s) - T0+30s = 90s
		clock.Advance(30 * time.Second)
		got, err := n.NormalizeDesiredReplicas(testKey, 10, 5, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 10 {
			t.Errorf("got %d, want 10 (held)", got.Replicas)
		}
		if got.RequeueAfter != 90*time.Second {
			t.Errorf("RequeueAfter = %v, want %v", got.RequeueAfter, 90*time.Second)
		}
	})

	t.Run("asymmetric windows compute RequeueAfter from correct direction", func(t *testing.T) {
		t.Parallel()

		// Scale-down test: upWindow=60s, downWindow=300s
		clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
		cfg := &config.Config{
			ScaleUpStabilizationWindow:   60 * time.Second,
			ScaleDownStabilizationWindow: 300 * time.Second,
		}
		n := newTestNormalizer(clock, cfg)
		b := simpleDynamic()

		// Seed at current level
		if _, err := n.NormalizeDesiredReplicas(testKey, 10, 10, b, log); err != nil {
			t.Fatal(err)
		}

		// T0+30s: recommend 5 -> held by seed (10@T0 >= 10)
		// RequeueAfter should be ~270s (300-30) from the down-window.
		clock.Advance(30 * time.Second)
		got, err := n.NormalizeDesiredReplicas(testKey, 10, 5, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 10 {
			t.Errorf("got %d, want 10 (held)", got.Replicas)
		}
		if got.RequeueAfter != 270*time.Second {
			t.Errorf("RequeueAfter = %v, want %v (from down-window)", got.RequeueAfter, 270*time.Second)
		}

		// Scale-up test: upWindow=300s, downWindow=60s
		clock2 := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
		cfg2 := &config.Config{
			ScaleUpStabilizationWindow:   300 * time.Second,
			ScaleDownStabilizationWindow: 60 * time.Second,
		}
		n2 := newTestNormalizer(clock2, cfg2)
		key2 := types.NamespacedName{Namespace: "test", Name: "cache2"}
		if _, err := n2.NormalizeDesiredReplicas(key2, 10, 10, b, log); err != nil {
			t.Fatal(err)
		}

		// T0+30s: recommend 20 -> held because 10@T0 is in up-window (<= current)
		// RequeueAfter should use the scale-UP window (300s): (0 + 300s) - 30s = 270s
		clock2.Advance(30 * time.Second)
		got, err = n2.NormalizeDesiredReplicas(key2, 10, 20, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != 10 {
			t.Errorf("scale-up: got %d, want 10 (held)", got.Replicas)
		}
		if got.RequeueAfter != 270*time.Second {
			t.Errorf("scale-up RequeueAfter = %v, want %v (from up-window)", got.RequeueAfter, 270*time.Second)
		}
	})
}

func TestNormalizeDesiredReplicas_Purge(t *testing.T) {
	log := newTestLogger(t)
	t.Parallel()
	clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
	cfg := &config.Config{ScaleDownStabilizationWindow: 5 * time.Minute}
	n := newTestNormalizer(clock, cfg)
	b := simpleDynamic()

	// Seed a recommendation
	if _, err := n.NormalizeDesiredReplicas(testKey, 10, 5, b, log); err != nil {
		t.Fatal(err)
	}

	// Purge and verify history is cleared
	n.Purge(testKey)
	clock.Advance(1 * time.Second)

	// After purge, a call with desired == current confirms clean state
	got, err := n.NormalizeDesiredReplicas(testKey, 5, 5, b, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 5 {
		t.Errorf("after purge (no-op): got %d, want 5", got.Replicas)
	}
}

func TestResetIfScalingSpecChanged(t *testing.T) {
	log := newTestLogger(t)
	t.Parallel()
	clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
	cfg := &config.Config{ScaleDownStabilizationWindow: 5 * time.Minute}
	n := newTestNormalizer(clock, cfg)
	dynamic := simpleDynamic()

	// Seed stabilization state.
	if _, err := n.NormalizeDesiredReplicas(testKey, 10, 10, dynamic, log); err != nil {
		t.Fatal(err)
	}

	// 30s later: recommend 5 -> stabilization should hold at 10.
	clock.Advance(30 * time.Second)
	got, err := n.NormalizeDesiredReplicas(testKey, 10, 5, dynamic, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 10 {
		t.Errorf("before spec change: got %d, want 10 (held by stabilization)", got.Replicas)
	}

	// Same spec -> no reset, stabilization still holds.
	clock.Advance(1 * time.Second)
	got, err = n.NormalizeDesiredReplicas(testKey, 10, 5, dynamic, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 10 {
		t.Errorf("same spec: got %d, want 10 (still held)", got.Replicas)
	}

	// Change spec -> resets stabilization, scales immediately.
	changed := dynamic.DeepCopy()
	changed.PercentageOfNodesToUse = 25
	clock.Advance(1 * time.Second)
	got, err = n.NormalizeDesiredReplicas(testKey, 10, 5, changed, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 5 {
		t.Errorf("after spec change: got %d, want 5 (immediate)", got.Replicas)
	}
}

// TestScenario_ControllerRestart verifies that after a controller restart
// (normalizer state lost), the first reconciliation scales immediately to
// the desired value rather than waiting for a stabilization window.
func TestScenario_ControllerRestart(t *testing.T) {
	log := newTestLogger(t)
	t.Parallel()
	clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
	n := newTestNormalizer(clock, defaultScalingConfig()) // fresh normalizer = simulates restart
	b := simpleDynamic()
	key := types.NamespacedName{Namespace: "restart", Name: "cache"}

	// First call after restart: current=10, desired=15. Scales immediately.
	got, err := n.NormalizeDesiredReplicas(key, 10, 15, b, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 15 {
		t.Errorf("after restart: got %d, want 15 (first observation scales immediately)", got.Replicas)
	}

	// Second call with a different desired should be stabilized (state exists now)
	clock.Advance(1 * time.Second)
	got, err = n.NormalizeDesiredReplicas(key, 15, 20, b, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 15 {
		t.Errorf("second call: got %d, want 15 (should be held by window)", got.Replicas)
	}
}

// TestScenario_LongReconciliationGap verifies that after a long gap (e.g.
// the controller was busy with other CRs), the normalizer scales immediately
// because the stabilization window has long since expired. State is in-memory
// so a controller restart would clear it and the first-observation path
// handles that case.
func TestScenario_LongReconciliationGap(t *testing.T) {
	log := newTestLogger(t)
	t.Parallel()
	clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
	cfg := &config.Config{ScaleDownStabilizationWindow: 5 * time.Minute}
	n := newTestNormalizer(clock, cfg)
	b := simpleDynamic()
	key := types.NamespacedName{Namespace: "gap", Name: "cache"}

	// Establish steady state at 10 replicas
	if _, err := n.NormalizeDesiredReplicas(key, 10, 10, b, log); err != nil {
		t.Fatal(err)
	}

	// 15 minutes pass -- well beyond the 5-min window.
	clock.Advance(15 * time.Minute)

	// Desired drops to 5. The stabilization window expired long ago
	// (lastAtOrAbove was 15 min ago), so scale-down proceeds immediately.
	got, err := n.NormalizeDesiredReplicas(key, 10, 5, b, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 5 {
		t.Errorf("after gap: got %d, want 5 (window expired, should scale immediately)", got.Replicas)
	}
	if got.RequeueAfter != 0 {
		t.Errorf("after gap: RequeueAfter = %v, want 0", got.RequeueAfter)
	}
}

// TestScenario_AKSNodePoolScaleUp simulates AKS scaling a node pool from 10 to
// 20 nodes over several minutes. With 50% percentageOfNodesToUse the raw desired
// replicas increase from 5->6->7->8->9->10 as nodes become Ready one by one.
//
// During the ramp (phase 1), the stabilization window holds scaling because
// the seed recommendation (5) is still in the window. After the window expires
// (phase 2), a single scale-up to 10 occurs -- no staircase stepping through
// intermediate values.
func TestScenario_AKSNodePoolScaleUp(t *testing.T) {
	log := newTestLogger(t)
	t.Parallel()
	clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
	n := newTestNormalizer(clock, defaultScalingConfig())
	b := simpleDynamic()
	key := types.NamespacedName{Namespace: "aks", Name: "scaleup"}

	// Steady state: 10 nodes -> 5 replicas (50%).
	got, err := n.NormalizeDesiredReplicas(key, 5, 5, b, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 5 {
		t.Fatalf("initial: got %d, want 5", got.Replicas)
	}

	// Phase 1: AKS adds nodes over 4 minutes. Each node event triggers
	// reconciliation with an increasing raw desired count.
	scaleChanges := 0
	currentReplicas := int32(5)
	nodeArrivals := []struct {
		rawDesired int32
		delay      time.Duration
	}{
		{6, 1 * time.Minute}, // t=1m: 12 nodes
		{7, 1 * time.Minute}, // t=2m: 14 nodes
		{8, 1 * time.Minute}, // t=3m: 16 nodes
		{9, 1 * time.Minute}, // t=4m: 18 nodes
	}

	for _, arrival := range nodeArrivals {
		clock.Advance(arrival.delay)
		got, err = n.NormalizeDesiredReplicas(key, currentReplicas, arrival.rawDesired, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != currentReplicas {
			scaleChanges++
			currentReplicas = got.Replicas
		}
	}

	if scaleChanges != 0 {
		t.Errorf("during node arrivals: got %d scale changes, want 0 (stabilization should hold)", scaleChanges)
	}

	// Phase 2: At t=15m the window expires (desired has been continuously
	// above current for 15 full minutes). A single jump to 10.
	clock.Advance(11 * time.Minute) // t=15m
	got, err = n.NormalizeDesiredReplicas(key, currentReplicas, 10, b, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 10 {
		t.Errorf("after window: got %d, want 10 (single jump)", got.Replicas)
	}
	if got.Replicas != currentReplicas {
		scaleChanges++
	}

	if scaleChanges != 1 {
		t.Errorf("total scale changes: got %d, want 1 (single ring rebalance)", scaleChanges)
	}
}

// TestScenario_AKSNodePoolScaleDown simulates AKS scaling a node pool from 20
// to 10 nodes over several minutes. The stabilization window batches this into
// a single scale-down -- no staircase through intermediate values.
func TestScenario_AKSNodePoolScaleDown(t *testing.T) {
	log := newTestLogger(t)
	t.Parallel()
	clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
	// Use a 6-minute window so it covers the full 5-minute departure ramp.
	cfg := &config.Config{
		ScaleUpStabilizationWindow:   6 * time.Minute,
		ScaleDownStabilizationWindow: 6 * time.Minute,
		ScaleUpTolerancePercent:      10,
		ScaleDownTolerancePercent:    10,
	}
	n := newTestNormalizer(clock, cfg)
	b := simpleDynamic()
	key := types.NamespacedName{Namespace: "aks", Name: "scaledown"}

	// Steady state: 20 nodes -> 10 replicas (50%).
	got, err := n.NormalizeDesiredReplicas(key, 10, 10, b, log)
	if err != nil {
		t.Fatal(err)
	}

	// Phase 1: Nodes removed over 5 minutes. All changes should be held
	// by stabilization (6-min window not yet expired).
	scaleChanges := 0
	currentReplicas := int32(10)
	nodeDepartures := []struct {
		rawDesired int32
		delay      time.Duration
	}{
		{9, 1 * time.Minute}, // within tolerance -> suppressed
		{8, 1 * time.Minute}, // 20% drop, passes tolerance but held by stabilization
		{7, 1 * time.Minute},
		{6, 1 * time.Minute},
		{5, 1 * time.Minute},
	}

	for _, dep := range nodeDepartures {
		clock.Advance(dep.delay)
		got, err = n.NormalizeDesiredReplicas(key, currentReplicas, dep.rawDesired, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != currentReplicas {
			scaleChanges++
			currentReplicas = got.Replicas
		}
	}

	if scaleChanges != 0 {
		t.Errorf("during node removals: got %d scale changes, want 0", scaleChanges)
	}

	// Phase 2: Wait for window to expire. The tolerance-suppressed observation
	// at t=1m (raw=9 clamped to current=10) refreshes lastAtOrAbove, so the
	// 6-min scale-down window runs from t=1m, not t=0. We need t=7m1s.
	clock.Advance(2*time.Minute + 1*time.Second) // t=7m1s
	got, err = n.NormalizeDesiredReplicas(key, currentReplicas, 5, b, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 5 {
		t.Errorf("after window: got %d, want 5 (single jump)", got.Replicas)
	}
	if got.Replicas != currentReplicas {
		scaleChanges++
	}

	if scaleChanges != 1 {
		t.Errorf("total scale changes: got %d, want 1 (single ring rebalance)", scaleChanges)
	}
}

// TestScenario_AKSUpgrade simulates an AKS node pool upgrade where surge nodes
// are added temporarily and then the old nodes are drained. The net node count
// returns to the original, so no scaling should happen at all.
//
// AKS upgrade with maxSurge=1 on a 10-node pool works roughly like:
//  1. Add 1 surge node (11 nodes)
//  2. Cordon+drain 1 old node (10 schedulable)
//  3. Delete old node, add surge node (11 schedulable briefly, then 10)
//  4. Repeat until all nodes are upgraded
//
// The schedulable node count oscillates between 10 and 11 throughout. With 50%
// percentageOfNodesToUse, desired oscillates between 5 and 6 -- well within the
// 10% tolerance band. No scaling should occur.
func TestScenario_AKSUpgrade(t *testing.T) {
	log := newTestLogger(t)
	t.Parallel()
	clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
	n := newTestNormalizer(clock, defaultScalingConfig())
	b := simpleDynamic()
	key := types.NamespacedName{Namespace: "aks", Name: "upgrade"}

	// Steady state: 10 nodes -> 5 replicas.
	got, err := n.NormalizeDesiredReplicas(key, 5, 5, b, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 5 {
		t.Fatalf("initial: got %d, want 5", got.Replicas)
	}

	// Simulate 10 node upgrade cycles (~2 min each).
	// Each cycle: surge adds 1 node (11 schedulable -> raw=6), then drain
	// removes 1 old node (10 schedulable -> raw=5).
	currentReplicas := int32(5)
	scaleChanges := 0

	for cycle := 0; cycle < 10; cycle++ {
		// Surge node added: 11 schedulable -> 6 desired (20% up from 5)
		clock.Advance(30 * time.Second)
		got, err = n.NormalizeDesiredReplicas(key, currentReplicas, 6, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != currentReplicas {
			scaleChanges++
			currentReplicas = got.Replicas
		}

		// Old node cordoned+drained: 10 schedulable -> 5 desired
		clock.Advance(30 * time.Second)
		got, err = n.NormalizeDesiredReplicas(key, currentReplicas, 5, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != currentReplicas {
			scaleChanges++
			currentReplicas = got.Replicas
		}

		// Upgraded node ready, surge node for next cycle: back to 10 -> 5
		clock.Advance(1 * time.Minute)
		got, err = n.NormalizeDesiredReplicas(key, currentReplicas, 5, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != currentReplicas {
			scaleChanges++
			currentReplicas = got.Replicas
		}
	}

	if scaleChanges != 0 {
		t.Errorf("during AKS upgrade: got %d scale changes, want 0 (tolerance should suppress)", scaleChanges)
	}
	if currentReplicas != 5 {
		t.Errorf("final replicas: got %d, want 5 (unchanged)", currentReplicas)
	}
}

// TestScenario_RapidNodeDeletion simulates multiple nodes being deleted within
// a single second (e.g., AKS VMSS scale-in deleting several instances at once).
// The stabilization window batches this into a single scale-down even when
// observations arrive in sub-second bursts. This complements
// TestScenario_AKSNodePoolScaleDown which uses 1-minute intervals.
func TestScenario_RapidNodeDeletion(t *testing.T) {
	log := newTestLogger(t)
	t.Parallel()
	clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
	cfg := &config.Config{
		ScaleUpStabilizationWindow:   5 * time.Minute,
		ScaleDownStabilizationWindow: 5 * time.Minute,
		ScaleUpTolerancePercent:      10,
		ScaleDownTolerancePercent:    10,
	}
	n := newTestNormalizer(clock, cfg)
	b := simpleDynamic()
	key := types.NamespacedName{Namespace: "aks", Name: "rapid-delete"}

	// Steady state: 20 nodes -> 10 replicas (50%).
	got, err := n.NormalizeDesiredReplicas(key, 10, 10, b, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 10 {
		t.Fatalf("initial: got %d, want 10", got.Replicas)
	}

	// Phase 1: 6 nodes deleted within 1 second. Each deletion triggers a
	// reconcile with decreasing rawDesired. All should be held by
	// stabilization because the window just started.
	scaleChanges := 0
	currentReplicas := int32(10)
	burstDeletions := []struct {
		rawDesired int32
		delay      time.Duration
	}{
		{9, 100 * time.Millisecond},
		{8, 150 * time.Millisecond},
		{7, 200 * time.Millisecond},
		{6, 100 * time.Millisecond},
		{5, 200 * time.Millisecond},
		{4, 250 * time.Millisecond}, // total elapsed: 1s
	}

	for _, del := range burstDeletions {
		clock.Advance(del.delay)
		got, err = n.NormalizeDesiredReplicas(key, currentReplicas, del.rawDesired, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != currentReplicas {
			scaleChanges++
			currentReplicas = got.Replicas
		}
	}

	if scaleChanges != 0 {
		t.Errorf("during burst deletions: got %d scale changes, want 0 (stabilization should hold)", scaleChanges)
	}

	// Phase 2: Wait for the stabilization window to expire. The window
	// should be measured from the seed observation (T=0), not from the
	// last burst event (T+1s).
	clock.Advance(5 * time.Minute) // t=5m1s, window started at T=0
	got, err = n.NormalizeDesiredReplicas(key, currentReplicas, 4, b, log)
	if err != nil {
		t.Fatal(err)
	}
	if got.Replicas != 4 {
		t.Errorf("after window: got %d, want 4 (single jump to latest desired)", got.Replicas)
	}
	if got.Replicas != currentReplicas {
		scaleChanges++
	}

	if scaleChanges != 1 {
		t.Errorf("total scale changes: got %d, want 1 (single ring rebalance)", scaleChanges)
	}
}

// TestScenario_AKSUpgradeLargePool simulates an AKS upgrade on a larger pool
// (100 nodes) with maxSurge=10. Even with 10 extra surge nodes (10% increase),
// tolerance suppresses scaling entirely.
func TestScenario_AKSUpgradeLargePool(t *testing.T) {
	log := newTestLogger(t)
	t.Parallel()
	clock := &FakeClock{T: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)}
	n := newTestNormalizer(clock, defaultScalingConfig())
	b := simpleDynamic()
	key := types.NamespacedName{Namespace: "aks", Name: "upgrade-large"}

	// Steady state: 100 nodes -> 50 replicas (50%).
	got, err := n.NormalizeDesiredReplicas(key, 50, 50, b, log)
	if err != nil {
		t.Fatal(err)
	}

	currentReplicas := int32(50)
	scaleChanges := 0

	// 10 upgrade batches, 10 nodes per batch.
	// Surge: 110 schedulable -> 55 desired (10% up from 50, at tolerance edge).
	// After drain: back to 100 -> 50 desired.
	for batch := 0; batch < 10; batch++ {
		// Surge nodes arrive
		clock.Advance(30 * time.Second)
		got, err = n.NormalizeDesiredReplicas(key, currentReplicas, 55, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != currentReplicas {
			scaleChanges++
			currentReplicas = got.Replicas
		}

		// Old nodes drained
		clock.Advance(2 * time.Minute)
		got, err = n.NormalizeDesiredReplicas(key, currentReplicas, 50, b, log)
		if err != nil {
			t.Fatal(err)
		}
		if got.Replicas != currentReplicas {
			scaleChanges++
			currentReplicas = got.Replicas
		}
	}

	if scaleChanges != 0 {
		t.Errorf("during large pool upgrade: got %d scale changes, want 0", scaleChanges)
	}
	if currentReplicas != 50 {
		t.Errorf("final replicas: got %d, want 50 (unchanged)", currentReplicas)
	}
}

// TestScenario_ToleranceSuppressedThenAboveBand verifies that
// tolerance-suppressed observations do not accumulate stabilization credit.
//
// Sequence:
//  1. Seed at current=10, desired=10 (steady state).
//  2. T+1m: desired=11 (within 10% tolerance, suppressed to 10).
//  3. T+3m: desired=11 again (still suppressed).
//  4. T+5m1s: desired=12 (above tolerance: 12/10=1.2 > 1.1).
//     The stabilization window is 5 minutes. If tolerance-suppressed
//     observations refreshed the stabilization timestamps correctly,
//     the last "at or below" timestamp was at T+3m, so only 2m1s have
//     elapsed - not enough to clear the 5m window. Scale-up must be blocked.
func TestScenario_ToleranceSuppressedThenAboveBand(t *testing.T) {
	log := newTestLogger(t)
	clock := &FakeClock{T: time.Date(2025, 1, 1, 0, 0, 0, 0, time.UTC)}

	cfg := &config.Config{
		ScaleUpStabilizationWindow:   5 * time.Minute,
		ScaleDownStabilizationWindow: 5 * time.Minute,
		ScaleUpTolerancePercent:      10,
		ScaleDownTolerancePercent:    10,
	}
	n := newTestNormalizer(clock, cfg)
	key := types.NamespacedName{Namespace: "test", Name: "tol-stab"}
	b := simpleDynamic()

	var currentReplicas int32 = 10

	// Seed: desired=10, current=10 - establishes both timestamps.
	got, err := n.NormalizeDesiredReplicas(key, currentReplicas, 10, b, log)
	if err != nil {
		t.Fatalf("seed: %v", err)
	}
	if got.Replicas != 10 {
		t.Fatalf("seed: got %d, want 10", got.Replicas)
	}

	// T+1m: desired=11 - within 10% tolerance (11/10=1.1), suppressed.
	clock.Advance(1 * time.Minute)
	got, err = n.NormalizeDesiredReplicas(key, currentReplicas, 11, b, log)
	if err != nil {
		t.Fatalf("T+1m: %v", err)
	}
	if got.Replicas != 10 {
		t.Fatalf("T+1m: got %d, want 10 (tolerance suppressed)", got.Replicas)
	}

	// T+3m: desired=11 again - still within tolerance, suppressed.
	clock.Advance(2 * time.Minute)
	got, err = n.NormalizeDesiredReplicas(key, currentReplicas, 11, b, log)
	if err != nil {
		t.Fatalf("T+3m: %v", err)
	}
	if got.Replicas != 10 {
		t.Fatalf("T+3m: got %d, want 10 (tolerance suppressed)", got.Replicas)
	}

	// T+5m1s: desired=12 - above tolerance (12/10=1.2 > 1.1).
	// Even though >5m have passed since the seed, the stabilization window
	// should NOT have expired because the T+3m observation (suppressed to
	// current) refreshed the "at or below" timestamp. Only 2m1s have
	// elapsed since T+3m.
	clock.Advance(2*time.Minute + 1*time.Second)
	got, err = n.NormalizeDesiredReplicas(key, currentReplicas, 12, b, log)
	if err != nil {
		t.Fatalf("T+5m1s: %v", err)
	}
	if got.Replicas != 10 {
		t.Errorf("T+5m1s: got %d, want 10 (stabilization should block scale-up)", got.Replicas)
	}
	if got.RequeueAfter == 0 {
		t.Error("T+5m1s: expected non-zero RequeueAfter for pending stabilization")
	}
}
