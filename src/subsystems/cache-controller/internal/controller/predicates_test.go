/*
Copyright (c) Microsoft Corporation.
Licensed under the Apache License, Version 2.0.
*/

package controller

import (
	"testing"

	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"sigs.k8s.io/controller-runtime/pkg/event"
)

// ---------------------------------------------------------------------------
// cacheserverPodPredicate tests
// ---------------------------------------------------------------------------

func baseCacheserverPod() *corev1.Pod {
	return &corev1.Pod{
		ObjectMeta: metav1.ObjectMeta{
			Name:      "cacheserver-0",
			Namespace: "default",
			Labels:    map[string]string{"app": "cacheserver"},
		},
		Spec: corev1.PodSpec{
			NodeName: "node-1",
		},
		Status: corev1.PodStatus{
			Phase: corev1.PodRunning,
			ContainerStatuses: []corev1.ContainerStatus{
				{Name: "cache", Ready: true},
			},
		},
	}
}

func TestCacheserverPodPredicate_RejectsNonCacheserver(t *testing.T) {
	t.Parallel()
	pred := cacheserverPodPredicate{}

	pod := baseCacheserverPod()
	pod.Labels = map[string]string{"app": "nginx"}

	if pred.Create(event.CreateEvent{Object: pod}) {
		t.Error("expected Create to reject non-cacheserver pod")
	}
	if pred.Delete(event.DeleteEvent{Object: pod}) {
		t.Error("expected Delete to reject non-cacheserver pod")
	}
}

func TestCacheserverPodPredicate_AcceptsCacheserver(t *testing.T) {
	t.Parallel()
	pred := cacheserverPodPredicate{}
	pod := baseCacheserverPod()

	if !pred.Create(event.CreateEvent{Object: pod}) {
		t.Error("expected Create to accept cacheserver pod")
	}
	if !pred.Delete(event.DeleteEvent{Object: pod}) {
		t.Error("expected Delete to accept cacheserver pod")
	}
}

func TestCacheserverPodPredicate_Update(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name   string
		modify func(old, new *corev1.Pod)
		want   bool
	}{
		{
			name:   "no change",
			modify: func(_, _ *corev1.Pod) {},
			want:   false,
		},
		{
			name: "phase changed",
			modify: func(_, newPod *corev1.Pod) {
				newPod.Status.Phase = corev1.PodFailed
			},
			want: true,
		},
		{
			name: "node name assigned",
			modify: func(oldPod, newPod *corev1.Pod) {
				oldPod.Spec.NodeName = ""
				newPod.Spec.NodeName = "node-1"
			},
			want: true,
		},
		{
			name: "deletion timestamp set",
			modify: func(_, newPod *corev1.Pod) {
				now := metav1.Now()
				newPod.DeletionTimestamp = &now
			},
			want: true,
		},
		{
			name: "container readiness changed",
			modify: func(_, newPod *corev1.Pod) {
				newPod.Status.ContainerStatuses[0].Ready = false
			},
			want: true,
		},
		{
			name: "annotation change ignored",
			modify: func(_, newPod *corev1.Pod) {
				newPod.Annotations = map[string]string{"foo": "bar"}
			},
			want: false,
		},
		{
			name: "resource version change ignored",
			modify: func(_, newPod *corev1.Pod) {
				newPod.ResourceVersion = "999"
			},
			want: false,
		},
	}

	pred := cacheserverPodPredicate{}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			t.Parallel()
			oldPod := baseCacheserverPod()
			newPod := baseCacheserverPod()
			tc.modify(oldPod, newPod)

			got := pred.Update(event.UpdateEvent{ObjectOld: oldPod, ObjectNew: newPod})
			if got != tc.want {
				t.Errorf("Update() = %v, want %v", got, tc.want)
			}
		})
	}
}

// ---------------------------------------------------------------------------
// stsStatusChangedPredicate tests
// ---------------------------------------------------------------------------

func TestStsStatusChangedPredicate_Update(t *testing.T) {
	t.Parallel()

	baseSts := func() *appsv1.StatefulSet {
		return &appsv1.StatefulSet{
			ObjectMeta: metav1.ObjectMeta{Name: "cacheserver", Namespace: "default"},
			Status: appsv1.StatefulSetStatus{
				Replicas:      3,
				ReadyReplicas: 3,
			},
		}
	}

	tests := []struct {
		name string
		old  *appsv1.StatefulSet
		new  *appsv1.StatefulSet
		want bool
	}{
		{
			name: "no change",
			old:  baseSts(),
			new:  baseSts(),
			want: false,
		},
		{
			name: "readyReplicas changed",
			old:  baseSts(),
			new: func() *appsv1.StatefulSet {
				s := baseSts()
				s.Status.ReadyReplicas = 2
				return s
			}(),
			want: true,
		},
		{
			name: "replicas changed",
			old:  baseSts(),
			new: func() *appsv1.StatefulSet {
				s := baseSts()
				s.Status.Replicas = 4
				return s
			}(),
			want: true,
		},
		{
			name: "both changed",
			old:  baseSts(),
			new: func() *appsv1.StatefulSet {
				s := baseSts()
				s.Status.Replicas = 5
				s.Status.ReadyReplicas = 4
				return s
			}(),
			want: true,
		},
		{
			name: "only annotations changed",
			old:  baseSts(),
			new: func() *appsv1.StatefulSet {
				s := baseSts()
				s.Annotations = map[string]string{"foo": "bar"}
				return s
			}(),
			want: false,
		},
		{
			name: "only currentReplicas changed",
			old:  baseSts(),
			new: func() *appsv1.StatefulSet {
				s := baseSts()
				s.Status.CurrentReplicas = 2
				return s
			}(),
			want: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()
			p := stsStatusChangedPredicate{}
			got := p.Update(event.UpdateEvent{
				ObjectOld: tt.old,
				ObjectNew: tt.new,
			})
			if got != tt.want {
				t.Fatalf("got %v, want %v", got, tt.want)
			}
		})
	}
}

func TestStsStatusChangedPredicate_NonStsObjects(t *testing.T) {
	t.Parallel()
	p := stsStatusChangedPredicate{}
	got := p.Update(event.UpdateEvent{
		ObjectOld: &corev1.Pod{ObjectMeta: metav1.ObjectMeta{Name: "p1"}},
		ObjectNew: &corev1.Pod{ObjectMeta: metav1.ObjectMeta{Name: "p1"}},
	})
	if got {
		t.Fatal("expected false for non-StatefulSet objects")
	}
}
