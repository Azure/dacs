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

package management

import (
	"testing"
	"time"

	"gotest.tools/v3/assert"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

// validNonEphemeralPod returns a well-formed cacheserver pod using HostPath storage.
func validNonEphemeralPod() corev1.Pod {
	return corev1.Pod{
		ObjectMeta: metav1.ObjectMeta{Name: "cs-0", Namespace: "ns"},
		Spec: corev1.PodSpec{
			SchedulerName: ControllerName,
			NodeName:      "node-1",
			Containers:    []corev1.Container{{Name: "cacheserver", Image: "img"}},
			Volumes: []corev1.Volume{{
				Name:         CacheDataVolumeName,
				VolumeSource: corev1.VolumeSource{HostPath: &corev1.HostPathVolumeSource{Path: "/mnt"}},
			}},
			Affinity: &corev1.Affinity{
				NodeAffinity: &corev1.NodeAffinity{
					RequiredDuringSchedulingIgnoredDuringExecution: &corev1.NodeSelector{
						NodeSelectorTerms: []corev1.NodeSelectorTerm{{
							MatchExpressions: []corev1.NodeSelectorRequirement{{
								Key:      "agentpool",
								Operator: corev1.NodeSelectorOpIn,
								Values:   []string{"user"},
							}},
						}},
					},
				},
			},
		},
		Status: corev1.PodStatus{Phase: corev1.PodRunning},
	}
}

// validEphemeralPod returns a well-formed cacheserver pod using EmptyDir storage.
func validEphemeralPod() corev1.Pod {
	return corev1.Pod{
		ObjectMeta: metav1.ObjectMeta{Name: "cs-0", Namespace: "ns"},
		Spec: corev1.PodSpec{
			SchedulerName: "default-scheduler",
			NodeName:      "node-1",
			Containers:    []corev1.Container{{Name: "cacheserver", Image: "img"}},
			Volumes: []corev1.Volume{{
				Name:         CacheDataVolumeName,
				VolumeSource: corev1.VolumeSource{EmptyDir: &corev1.EmptyDirVolumeSource{}},
			}},
		},
		Status: corev1.PodStatus{Phase: corev1.PodRunning},
	}
}

// ---------- sanityCheckPod tests ----------

func TestSanityCheckPod_ValidNonEphemeral(t *testing.T) {
	pod := validNonEphemeralPod()
	assert.Assert(t, sanityCheckPod(&pod), "valid non-ephemeral pod should pass sanity check")
}

func TestSanityCheckPod_ValidEphemeral(t *testing.T) {
	pod := validEphemeralPod()
	assert.Assert(t, sanityCheckPod(&pod), "valid ephemeral pod should pass sanity check")
}

func TestSanityCheckPod_NoNodeButNotPending(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Spec.NodeName = ""
	pod.Status.Phase = corev1.PodRunning
	assert.Assert(t, !sanityCheckPod(&pod), "pod with no node in Running phase should fail")
}

func TestSanityCheckPod_PendingWithNoNode(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Spec.NodeName = ""
	pod.Status.Phase = corev1.PodPending
	assert.Assert(t, sanityCheckPod(&pod), "pending pod with no node is valid")
}

func TestSanityCheckPod_NoVolumes(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Spec.Volumes = nil
	assert.Assert(t, !sanityCheckPod(&pod), "pod with no volumes should fail")
}

func TestSanityCheckPod_WrongVolumeName(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Spec.Volumes[0].Name = "wrong-name"
	assert.Assert(t, !sanityCheckPod(&pod), "pod without cache-data volume should fail")
}

func TestSanityCheckPod_NonEphemeralMissingAffinity(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Spec.Affinity = nil
	assert.Assert(t, !sanityCheckPod(&pod), "non-ephemeral pod without affinity should fail")
}

func TestSanityCheckPod_NonEphemeralMissingNodeAffinity(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Spec.Affinity.NodeAffinity = nil
	assert.Assert(t, !sanityCheckPod(&pod), "non-ephemeral pod without nodeAffinity should fail")
}

func TestSanityCheckPod_NonEphemeralWrongOperator(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.
		NodeSelectorTerms[0].MatchExpressions[0].Operator = corev1.NodeSelectorOpNotIn
	assert.Assert(t, !sanityCheckPod(&pod), "non-ephemeral pod with NotIn operator should fail")
}

func TestSanityCheckPod_NonEphemeralMultipleValues(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.
		NodeSelectorTerms[0].MatchExpressions[0].Values = []string{"a", "b"}
	assert.Assert(t, !sanityCheckPod(&pod), "non-ephemeral pod with multiple match values should fail")
}

func TestSanityCheckPod_EphemeralWithCustomScheduler(t *testing.T) {
	pod := validEphemeralPod()
	pod.Spec.SchedulerName = ControllerName
	assert.Assert(t, !sanityCheckPod(&pod), "ephemeral pod with custom scheduler should fail")
}

// ---------- getPodState tests ----------

func TestGetPodState_Terminating(t *testing.T) {
	pod := validNonEphemeralPod()
	now := metav1.NewTime(time.Now())
	pod.DeletionTimestamp = &now
	assert.Equal(t, getPodState(&pod), PodStatusTerminating)
}

func TestGetPodState_Running(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Status.Phase = corev1.PodRunning
	pod.Status.ContainerStatuses = []corev1.ContainerStatus{
		{Name: "cacheserver", Ready: true},
	}
	assert.Equal(t, getPodState(&pod), PodStatusRunning)
}

func TestGetPodState_Pending(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Status.Phase = corev1.PodPending
	assert.Equal(t, getPodState(&pod), PodStatusPending)
}

func TestGetPodState_Failed(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Status.Phase = corev1.PodFailed
	assert.Equal(t, getPodState(&pod), PodStatusFailed)
}

func TestGetPodState_Succeeded(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Status.Phase = corev1.PodSucceeded
	assert.Equal(t, getPodState(&pod), PodStatusSucceeded)
}

func TestGetPodState_NotReady(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Status.Phase = corev1.PodRunning
	pod.Status.ContainerStatuses = []corev1.ContainerStatus{
		{Name: "cacheserver", Ready: false},
	}
	assert.Equal(t, getPodState(&pod), PodStatusNotReady)
}

func TestGetPodState_TerminatingTakesPrecedenceOverPhase(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Status.Phase = corev1.PodRunning
	pod.Status.ContainerStatuses = []corev1.ContainerStatus{
		{Name: "cacheserver", Ready: true},
	}
	now := metav1.NewTime(time.Now())
	pod.DeletionTimestamp = &now
	assert.Equal(t, getPodState(&pod), PodStatusTerminating,
		"terminating should take precedence over running phase")
}

func TestGetPodState_NotReadyTakesPrecedenceOverRunningPhase(t *testing.T) {
	pod := validNonEphemeralPod()
	pod.Status.Phase = corev1.PodRunning
	pod.Status.ContainerStatuses = []corev1.ContainerStatus{
		{Name: "cacheserver", Ready: true},
		{Name: "sidecar", Ready: false},
	}
	assert.Equal(t, getPodState(&pod), PodStatusNotReady,
		"any not-ready container should mark pod as NotReady")
}
