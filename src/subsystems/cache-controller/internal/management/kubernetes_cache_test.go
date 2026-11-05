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
	"context"
	"testing"

	"gotest.tools/v3/assert"
	corev1 "k8s.io/api/core/v1"
	apierrors "k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes/scheme"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/envtest"
)

func createValidCacheServerPod(name string, namespace string) *corev1.Pod {
	return &corev1.Pod{
		ObjectMeta: metav1.ObjectMeta{
			Name:      name,
			Namespace: namespace,
			Labels: map[string]string{
				PodLabelSelectorKey: PodLabelSelectorValue,
			},
		},
		Spec: corev1.PodSpec{
			SchedulerName: ControllerName,
			Containers: []corev1.Container{
				{
					Name:  "cacheserver",
					Image: "cacheserver:test",
				},
			},
			Volumes: []corev1.Volume{
				{
					Name: CacheDataVolumeName,
					VolumeSource: corev1.VolumeSource{
						HostPath: &corev1.HostPathVolumeSource{Path: "/mnt/cache"},
					},
				},
			},
			Affinity: &corev1.Affinity{
				NodeAffinity: &corev1.NodeAffinity{
					RequiredDuringSchedulingIgnoredDuringExecution: &corev1.NodeSelector{
						NodeSelectorTerms: []corev1.NodeSelectorTerm{
							{
								MatchExpressions: []corev1.NodeSelectorRequirement{
									{
										Key:      "agentpool",
										Operator: corev1.NodeSelectorOpIn,
										Values:   []string{"user"},
									},
								},
							},
						},
					},
				},
			},
		},
		Status: corev1.PodStatus{
			Phase: corev1.PodPending,
		},
	}
}

func TestGetNodesFromCache(t *testing.T) {
	testEnv := &envtest.Environment{}
	cfg, err := testEnv.Start()
	assert.NilError(t, err)
	defer func() {
		assert.NilError(t, testEnv.Stop())
	}()

	k8sClient, err := client.New(cfg, client.Options{Scheme: scheme.Scheme})
	assert.NilError(t, err)

	node1 := &corev1.Node{ObjectMeta: metav1.ObjectMeta{Name: "node-a", Labels: map[string]string{"k1": "v1"}}}
	node2 := &corev1.Node{ObjectMeta: metav1.ObjectMeta{Name: "node-b"}, Spec: corev1.NodeSpec{Unschedulable: true}}

	assert.NilError(t, k8sClient.Create(context.Background(), node1))
	assert.NilError(t, k8sClient.Create(context.Background(), node2))

	nodes, err := GetNodesFromCache(context.Background(), k8sClient)

	assert.NilError(t, err)
	assert.Equal(t, len(nodes), 2)

	seen := map[string]Node{}
	for _, node := range nodes {
		seen[node.NodeName] = node
	}
	assert.Equal(t, seen["node-a"].Schedulable, true)
	assert.Equal(t, seen["node-b"].Schedulable, false)
}

func TestGetPodsFromCache(t *testing.T) {
	testEnv := &envtest.Environment{}
	cfg, err := testEnv.Start()
	assert.NilError(t, err)
	defer func() {
		assert.NilError(t, testEnv.Stop())
	}()

	k8sClient, err := client.New(cfg, client.Options{Scheme: scheme.Scheme})
	assert.NilError(t, err)

	// Create namespace
	ns := &corev1.Namespace{ObjectMeta: metav1.ObjectMeta{Name: "ns1"}}
	assert.NilError(t, k8sClient.Create(context.Background(), ns))

	valid := createValidCacheServerPod("cacheserver-0", "ns1")
	ignored := createValidCacheServerPod("other-0", "ns1")
	ignored.Labels[PodLabelSelectorKey] = "other"

	assert.NilError(t, k8sClient.Create(context.Background(), valid))
	assert.NilError(t, k8sClient.Create(context.Background(), ignored))

	pods, err := GetPodsFromCache(context.Background(), k8sClient)

	assert.NilError(t, err)
	assert.Equal(t, len(pods), 1)
	assert.Equal(t, pods[0].PodName, "cacheserver-0")
	assert.Equal(t, pods[0].Namespace, "ns1")
	assert.Equal(t, pods[0].UID, valid.UID)
	assert.Equal(t, pods[0].NodeSelectorKey, "agentpool")
	assert.Equal(t, pods[0].NodeSelectorValue, "user")
}

func TestDeleteInstructionOnlyDeletesObservedPodUID(t *testing.T) {
	testEnv := &envtest.Environment{}
	cfg, err := testEnv.Start()
	assert.NilError(t, err)
	defer func() {
		assert.NilError(t, testEnv.Stop())
	}()

	k8sClient, err := client.New(cfg, client.Options{Scheme: scheme.Scheme})
	assert.NilError(t, err)
	ctx := context.Background()
	ns := &corev1.Namespace{ObjectMeta: metav1.ObjectMeta{Name: "delete-test"}}
	assert.NilError(t, k8sClient.Create(ctx, ns))

	for _, name := range []string{"original", "replacement", "missing-uid"} {
		t.Run(name, func(t *testing.T) {
			pod := createValidCacheServerPod(name, ns.Name)
			assert.NilError(t, k8sClient.Create(ctx, pod))
			instructions := []SchedulerInstruction{{
				Type:      DeleteInstruction,
				PodName:   pod.Name,
				Namespace: pod.Namespace,
				PodUID:    pod.UID,
			}}
			if name == "replacement" {
				assert.NilError(t, k8sClient.Delete(ctx, pod, client.GracePeriodSeconds(0)))
				pod = createValidCacheServerPod(name, ns.Name)
				assert.NilError(t, k8sClient.Create(ctx, pod))
				assert.Assert(t, pod.UID != instructions[0].PodUID)
			} else if name == "missing-uid" {
				instructions[0].PodUID = ""
			}

			errors := ExecuteSchedulerInstructionsOnK8s(ctx, k8sClient, instructions)
			var remaining corev1.Pod
			err := k8sClient.Get(ctx, client.ObjectKeyFromObject(pod), &remaining)
			if name == "original" {
				assert.Equal(t, len(errors), 0)
				assert.Assert(t, apierrors.IsNotFound(err))
			} else {
				assert.Equal(t, len(errors), 1)
				if name == "replacement" {
					assert.Assert(t, apierrors.IsConflict(errors[0]))
				} else {
					assert.Error(t, errors[0], "refusing to delete pod missing-uid(delete-test) without UID precondition")
				}
				assert.NilError(t, err)
				assert.Equal(t, remaining.UID, pod.UID)
			}
		})
	}
}
