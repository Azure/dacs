/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package controller

import (
	"testing"

	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"sigs.k8s.io/controller-runtime/pkg/event"
)

func newNode(labels map[string]string, unschedulable bool) *corev1.Node {
	return &corev1.Node{
		ObjectMeta: metav1.ObjectMeta{
			Name:   "test-node",
			Labels: labels,
		},
		Spec: corev1.NodeSpec{
			Unschedulable: unschedulable,
		},
	}
}

func TestNodePredicate_Create(t *testing.T) {
	t.Parallel()
	p := NodePredicate{}

	got := p.Create(event.CreateEvent{Object: newNode(nil, false)})
	if !got {
		t.Error("Create() should always return true")
	}
}

func TestNodePredicate_Delete(t *testing.T) {
	t.Parallel()
	p := NodePredicate{}

	got := p.Delete(event.DeleteEvent{Object: newNode(nil, false)})
	if !got {
		t.Error("Delete() should always return true")
	}
}

func TestNodePredicate_Generic(t *testing.T) {
	t.Parallel()
	p := NodePredicate{}

	got := p.Generic(event.GenericEvent{Object: newNode(nil, false)})
	if got {
		t.Error("Generic() should always return false")
	}
}

func TestNodePredicate_Update(t *testing.T) {
	t.Parallel()
	p := NodePredicate{}

	tests := []struct {
		name    string
		oldNode *corev1.Node
		newNode *corev1.Node
		want    bool
	}{
		{
			name:    "no change",
			oldNode: newNode(map[string]string{"key": "val"}, false),
			newNode: newNode(map[string]string{"key": "val"}, false),
			want:    false,
		},
		{
			name:    "label added",
			oldNode: newNode(nil, false),
			newNode: newNode(map[string]string{"gpu": "true"}, false),
			want:    true,
		},
		{
			name:    "label removed",
			oldNode: newNode(map[string]string{"gpu": "true"}, false),
			newNode: newNode(nil, false),
			want:    true,
		},
		{
			name:    "label value changed",
			oldNode: newNode(map[string]string{"gpu": "a100"}, false),
			newNode: newNode(map[string]string{"gpu": "h100"}, false),
			want:    true,
		},
		{
			name:    "cordoned",
			oldNode: newNode(nil, false),
			newNode: newNode(nil, true),
			want:    true,
		},
		{
			name:    "uncordoned",
			oldNode: newNode(nil, true),
			newNode: newNode(nil, false),
			want:    true,
		},
		{
			name:    "labels and unschedulable both changed",
			oldNode: newNode(map[string]string{"a": "1"}, false),
			newNode: newNode(map[string]string{"b": "2"}, true),
			want:    true,
		},
		{
			name: "non-scaling field changed",
			oldNode: func() *corev1.Node {
				n := newNode(nil, false)
				n.Spec.PodCIDR = "10.0.0.0/24"
				return n
			}(),
			newNode: func() *corev1.Node {
				n := newNode(nil, false)
				n.Spec.PodCIDR = "10.0.1.0/24"
				return n
			}(),
			want: false,
		},
		{
			name:    "non-node objects",
			oldNode: nil,
			newNode: nil,
			want:    false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()
			e := event.UpdateEvent{}
			if tt.oldNode != nil {
				e.ObjectOld = tt.oldNode
			}
			if tt.newNode != nil {
				e.ObjectNew = tt.newNode
			}
			got := p.Update(e)
			if got != tt.want {
				t.Errorf("Update() = %v, want %v", got, tt.want)
			}
		})
	}
}
