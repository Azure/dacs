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

package statushelper

import (
	"testing"
	"time"

	"k8s.io/apimachinery/pkg/api/meta"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"sigs.k8s.io/controller-runtime/pkg/client/fake"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
)

var fakeClient = fake.NewClientBuilder().Build()

func newCache() *storagev1.Cache {
	return &storagev1.Cache{
		ObjectMeta: metav1.ObjectMeta{
			Name:      "test-cache",
			Namespace: "default",
		},
	}
}

// newHelper creates a Helper via New() and fails the test on error.
func newHelper(t *testing.T) (*Helper, *storagev1.Cache) {
	t.Helper()
	cache := newCache()
	h, err := New(cache, fakeClient)
	if err != nil {
		t.Fatalf("New() returned unexpected error: %v", err)
	}
	return h, cache
}

func TestSetCondition_AddsNew(t *testing.T) {
	h, cache := newHelper(t)

	h.SetCondition(metav1.Condition{
		Type:    storagev1.ReadyCondition,
		Status:  metav1.ConditionTrue,
		Reason:  storagev1.ReasonAllReplicasReady,
		Message: "all good",
	})

	c := meta.FindStatusCondition(cache.Status.Conditions, storagev1.ReadyCondition)
	if c == nil {
		t.Fatal("Ready condition not found")
	}
	if c.Status != metav1.ConditionTrue || c.Reason != storagev1.ReasonAllReplicasReady {
		t.Errorf("unexpected condition: %+v", c)
	}
	if c.LastTransitionTime.IsZero() {
		t.Error("LastTransitionTime should be set")
	}
}

func TestSetCondition_UpdatesSameType(t *testing.T) {
	h, cache := newHelper(t)

	h.SetCondition(metav1.Condition{
		Type:   storagev1.ReadyCondition,
		Status: metav1.ConditionTrue,
		Reason: storagev1.ReasonAllReplicasReady,
	})
	c := meta.FindStatusCondition(cache.Status.Conditions, storagev1.ReadyCondition)
	if c == nil {
		t.Fatal("Ready condition not found after first set")
	}
	firstTime := c.LastTransitionTime

	// Update with different status -- transition time should change.
	h.SetCondition(metav1.Condition{
		Type:    storagev1.ReadyCondition,
		Status:  metav1.ConditionFalse,
		Reason:  storagev1.ReasonSomeReplicasNotReady,
		Message: "1/2 ready",
	})

	c = meta.FindStatusCondition(cache.Status.Conditions, storagev1.ReadyCondition)
	if c == nil {
		t.Fatal("Ready condition not found after update")
	}
	if c.Status != metav1.ConditionFalse {
		t.Errorf("expected ConditionFalse, got %s", c.Status)
	}
	if c.Reason != storagev1.ReasonSomeReplicasNotReady {
		t.Errorf("expected reason %s, got %s", storagev1.ReasonSomeReplicasNotReady, c.Reason)
	}
	if !c.LastTransitionTime.After(firstTime.Time) && c.LastTransitionTime.Time != firstTime.Time {
		t.Error("LastTransitionTime should be updated on status change")
	}
}

func TestSetCondition_PreservesTransitionTimeOnSameStatus(t *testing.T) {
	h, cache := newHelper(t)

	h.SetCondition(metav1.Condition{
		Type:   storagev1.ReadyCondition,
		Status: metav1.ConditionFalse,
		Reason: storagev1.ReasonNotEvaluated,
	})
	c := meta.FindStatusCondition(cache.Status.Conditions, storagev1.ReadyCondition)
	if c == nil {
		t.Fatal("Ready condition not found")
	}
	firstTime := c.LastTransitionTime

	// Same status, different reason -- transition time should be preserved.
	h.SetCondition(metav1.Condition{
		Type:    storagev1.ReadyCondition,
		Status:  metav1.ConditionFalse,
		Reason:  storagev1.ReasonNoReplicasReady,
		Message: "updated message",
	})

	c = meta.FindStatusCondition(cache.Status.Conditions, storagev1.ReadyCondition)
	if c == nil {
		t.Fatal("Ready condition not found after update")
	}
	if c.Reason != storagev1.ReasonNoReplicasReady {
		t.Errorf("expected reason %s, got %s", storagev1.ReasonNoReplicasReady, c.Reason)
	}
	if c.Message != "updated message" {
		t.Errorf("expected updated message, got %s", c.Message)
	}
	if !c.LastTransitionTime.Equal(&firstTime) {
		t.Error("LastTransitionTime should be preserved when status unchanged")
	}
}

func TestSetCondition_MultipleTypes(t *testing.T) {
	h, cache := newHelper(t)

	h.SetCondition(metav1.Condition{
		Type:   storagev1.ReplicasReadyCondition,
		Status: metav1.ConditionTrue,
		Reason: storagev1.ReasonAllReplicasReady,
	})
	h.SetCondition(metav1.Condition{
		Type:   storagev1.ReadyCondition,
		Status: metav1.ConditionTrue,
		Reason: storagev1.ReasonAllConditionsMet,
	})

	if meta.FindStatusCondition(cache.Status.Conditions, storagev1.ReplicasReadyCondition) == nil {
		t.Error("expected ReplicasReady condition")
	}
	if meta.FindStatusCondition(cache.Status.Conditions, storagev1.ReadyCondition) == nil {
		t.Error("expected Ready condition")
	}
}

func TestSetReady(t *testing.T) {
	type subCondition struct {
		ct      string
		status  metav1.ConditionStatus
		reason  string
		message string
	}
	tests := []struct {
		name       string
		conditions []subCondition
		wantStatus metav1.ConditionStatus
		wantReason string
	}{
		{
			name: "true when all true",
			conditions: []subCondition{
				{storagev1.PodsScheduledCondition, metav1.ConditionTrue, storagev1.ReasonAllPodsScheduled, "scheduled"},
				{storagev1.ReplicasReadyCondition, metav1.ConditionTrue, storagev1.ReasonAllReplicasReady, "ready"},
			},
			wantStatus: metav1.ConditionTrue,
		},
		{
			name: "false when any false",
			conditions: []subCondition{
				{storagev1.PodsScheduledCondition, metav1.ConditionTrue, storagev1.ReasonAllPodsScheduled, "scheduled"},
				{storagev1.ReplicasReadyCondition, metav1.ConditionFalse, storagev1.ReasonNoReplicasReady, "not ready"},
			},
			wantStatus: metav1.ConditionFalse,
			wantReason: storagev1.ReasonNoReplicasReady,
		},
		{
			name: "false when any unknown",
			conditions: []subCondition{
				{storagev1.ReplicasReadyCondition, metav1.ConditionTrue, storagev1.ReasonAllReplicasReady, "ready"},
				{storagev1.PodsScheduledCondition, metav1.ConditionUnknown, storagev1.ReasonNotEvaluated, "error"},
			},
			wantStatus: metav1.ConditionFalse,
		},
		{
			name: "false when all unknown",
			conditions: []subCondition{
				{storagev1.PodsScheduledCondition, metav1.ConditionUnknown, storagev1.ReasonNotEvaluated, "pending"},
				{storagev1.ReplicasReadyCondition, metav1.ConditionUnknown, storagev1.ReasonNotEvaluated, "pending"},
			},
			wantStatus: metav1.ConditionFalse,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			h, cache := newHelper(t)
			for _, sc := range tt.conditions {
				h.SetCondition(metav1.Condition{
					Type:    sc.ct,
					Status:  sc.status,
					Reason:  sc.reason,
					Message: sc.message,
				})
			}
			h.SetReady()

			c := meta.FindStatusCondition(cache.Status.Conditions, storagev1.ReadyCondition)
			if c == nil {
				t.Fatal("Ready condition not found")
			}
			if c.Status != tt.wantStatus {
				t.Errorf("Ready status = %s, want %s", c.Status, tt.wantStatus)
			}
			if tt.wantReason != "" && c.Reason != tt.wantReason {
				t.Errorf("Ready reason = %s, want %s", c.Reason, tt.wantReason)
			}
		})
	}
}

func TestSetReady_SkipsExistingReadyCondition(t *testing.T) {
	h, cache := newHelper(t)

	// Call SetReady twice: the second call should produce the same result.
	h.MarkTrue(storagev1.PodsScheduledCondition, storagev1.ReasonAllPodsScheduled, "scheduled")
	h.MarkTrue(storagev1.ReplicasReadyCondition, storagev1.ReasonAllReplicasReady, "ready")
	h.SetReady()
	h.SetReady()

	readyCount := 0
	for _, c := range cache.Status.Conditions {
		if c.Type == storagev1.ReadyCondition {
			readyCount++
			if c.Status != metav1.ConditionTrue {
				t.Errorf("expected Ready=True, got %s", c.Status)
			}
		}
	}
	if readyCount != 1 {
		t.Errorf("expected exactly 1 Ready condition, got %d", readyCount)
	}
}

func TestSetRequeueAfter(t *testing.T) {
	tests := []struct {
		name      string
		durations []time.Duration
		want      time.Duration
	}{
		{
			name:      "shortest wins",
			durations: []time.Duration{5 * time.Minute, 2 * time.Minute, 10 * time.Minute},
			want:      2 * time.Minute,
		},
		{
			name:      "ignores zero and negative",
			durations: []time.Duration{3 * time.Minute, 0, -1 * time.Second},
			want:      3 * time.Minute,
		},
		{
			name:      "zero when empty",
			durations: nil,
			want:      0,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			h := &Helper{}
			for _, d := range tt.durations {
				h.SetRequeueAfter(d)
			}
			if got := h.RequeueAfter(); got != tt.want {
				t.Errorf("RequeueAfter() = %s, want %s", got, tt.want)
			}
		})
	}
}

func TestToCtrlResult(t *testing.T) {
	tests := []struct {
		name     string
		duration time.Duration
		want     time.Duration
	}{
		{
			name:     "requeue when set",
			duration: 30 * time.Second,
			want:     30 * time.Second,
		},
		{
			name:     "zero when empty",
			duration: 0,
			want:     0,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			h := &Helper{}
			if tt.duration > 0 {
				h.SetRequeueAfter(tt.duration)
			}
			if got := h.ToCtrlResult().RequeueAfter; got != tt.want {
				t.Errorf("ToCtrlResult().RequeueAfter = %s, want %s", got, tt.want)
			}
		})
	}
}

func TestNew_Errors(t *testing.T) {
	tests := []struct {
		name    string
		cache   *storagev1.Cache
		wantMsg string
	}{
		{
			name:    "nil cache",
			cache:   nil,
			wantMsg: "cache must not be nil",
		},
		{
			name:    "nil client",
			cache:   &storagev1.Cache{},
			wantMsg: "client must not be nil",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			h, err := New(tt.cache, nil)
			if err == nil {
				t.Fatalf("expected error containing %q, got nil", tt.wantMsg)
			}
			if h != nil {
				t.Errorf("expected nil helper, got %v", h)
			}
		})
	}
}

func TestNew_InitializesSubConditionsToUnknown(t *testing.T) {
	cache := &storagev1.Cache{}
	h, err := New(cache, fake.NewClientBuilder().Build())
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	expected := map[string]struct{}{
		storagev1.PodsScheduledCondition: {},
		storagev1.ReplicasReadyCondition: {},
	}
	for _, c := range h.cache.Status.Conditions {
		if _, ok := expected[c.Type]; !ok {
			continue
		}
		if c.Status != metav1.ConditionUnknown {
			t.Errorf("condition %s: expected status %s, got %s", c.Type, metav1.ConditionUnknown, c.Status)
		}
		if c.Reason != storagev1.ReasonNotEvaluated {
			t.Errorf("condition %s: expected reason %s, got %s", c.Type, storagev1.ReasonNotEvaluated, c.Reason)
		}
		delete(expected, c.Type)
	}
	for ct := range expected {
		t.Errorf("expected condition %s to be initialized, but not found", ct)
	}
}
