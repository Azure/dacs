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

package controller

import (
	"context"
	"encoding/json"
	"reflect"
	"strings"
	"testing"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/go-logr/logr"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/utils/ptr"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/client/fake"
)

func TestRenderTemplate_Tolerations(t *testing.T) {
	for _, tolerations := range [][]corev1.Toleration{
		nil,
		{},
		{{Key: "sku", Operator: corev1.TolerationOpEqual, Value: "gpu", Effect: corev1.TaintEffectNoSchedule}},
		{{Operator: corev1.TolerationOpExists, Effect: corev1.TaintEffectNoExecute, TolerationSeconds: ptr.To(int64(0))}},
		{{Key: "sku", Effect: corev1.TaintEffectNoExecute, TolerationSeconds: ptr.To(int64(30))}},
	} {
		values := baseRenderValues()
		values.Tolerations = tolerations
		var sts appsv1.StatefulSet
		if err := renderTemplateInto("templates/cacheserver-app.yaml", values, &sts); err != nil {
			t.Fatal(err)
		}
		got := sts.Spec.Template.Spec.Tolerations
		if len(got) != len(tolerations) || (len(got) > 0 && !reflect.DeepEqual(got, tolerations)) {
			t.Fatalf("rendered tolerations = %#v, want %#v", got, tolerations)
		}
		if len(got) > 0 && got[0].TolerationSeconds != nil {
			*got[0].TolerationSeconds = 999
			if *tolerations[0].TolerationSeconds == 999 {
				t.Fatal("rendered tolerations alias input pointers")
			}
		}
	}
}

func TestCacheTolerationsReconciliation(t *testing.T) {
	ctx := context.Background()
	scheme := runtime.NewScheme()
	for _, add := range []func(*runtime.Scheme) error{storagev1.AddToScheme, appsv1.AddToScheme} {
		if err := add(scheme); err != nil {
			t.Fatal(err)
		}
	}
	cache := &storagev1.Cache{}
	if err := json.Unmarshal([]byte(`{
		"metadata":{"name":"tolerations","namespace":"default","uid":"cache-uid"},
		"spec":{"serverSizeInGB":4,"serverThreads":8,"tolerations":[
			{"key":"sku","operator":"Equal","value":"gpu","effect":"NoSchedule"}
		]}
	}`), cache); err != nil {
		t.Fatal(err)
	}
	if err := cache.Default(ctx, cache); err != nil {
		t.Fatal(err)
	}
	c := fake.NewClientBuilder().WithScheme(scheme).WithObjects(cache).Build()
	r := &CacheReconciler{Client: c, Scheme: scheme}
	if err := r.reconcileStatefulSet(ctx, cache, 1); err != nil {
		t.Fatal(err)
	}
	sts := &appsv1.StatefulSet{}
	if err := c.Get(ctx, client.ObjectKeyFromObject(cache), sts); err != nil {
		t.Fatal(err)
	}
	want := []corev1.Toleration{{
		Key: "sku", Operator: corev1.TolerationOpEqual, Value: "gpu", Effect: corev1.TaintEffectNoSchedule,
	}}
	if !reflect.DeepEqual(sts.Spec.Template.Spec.Tolerations, want) {
		t.Fatalf("Cache tolerations lost: got %#v, want %#v", sts.Spec.Template.Spec.Tolerations, want)
	}
	taint := corev1.Taint{Key: "sku", Value: "gpu", Effect: corev1.TaintEffectNoSchedule}
	if !sts.Spec.Template.Spec.Tolerations[0].ToleratesTaint(logr.Discard(), &taint, false) {
		t.Fatal("CacheServer cannot tolerate sku=gpu:NoSchedule")
	}
	for _, container := range sts.Spec.Template.Spec.Containers {
		for name := range container.Resources.Requests {
			if strings.Contains(string(name), "gpu") {
				t.Fatalf("unexpected GPU request: %s", name)
			}
		}
		for name := range container.Resources.Limits {
			if strings.Contains(string(name), "gpu") {
				t.Fatalf("unexpected GPU limit: %s", name)
			}
		}
	}

	for _, tt := range []struct {
		name        string
		tolerations []corev1.Toleration
	}{
		{"update", []corev1.Toleration{{Key: "sku", Value: "gpu", Effect: corev1.TaintEffectNoExecute, TolerationSeconds: ptr.To(int64(30))}}},
		{"zero seconds", []corev1.Toleration{{Key: "sku", Value: "gpu", Effect: corev1.TaintEffectNoExecute, TolerationSeconds: ptr.To(int64(0))}}},
		{"remove", nil},
		{"empty", []corev1.Toleration{}},
		{"restore", want},
	} {
		t.Run(tt.name, func(t *testing.T) {
			cache.Spec.Tolerations = tt.tolerations
			if err := c.Update(ctx, cache); err != nil {
				t.Fatal(err)
			}

			// A fresh reconciler reads the persisted Cache, as after a restart.
			r = &CacheReconciler{Client: c, Scheme: scheme}
			fresh := &storagev1.Cache{}
			if err := c.Get(ctx, client.ObjectKeyFromObject(cache), fresh); err != nil {
				t.Fatal(err)
			}
			for _, action := range []string{"reconcile", "repeat", "repair drift", "recreate"} {
				switch action {
				case "repair drift":
					sts.Spec.Template.Spec.Tolerations = []corev1.Toleration{{Key: "manual", Operator: corev1.TolerationOpExists}}
					if err := c.Update(ctx, sts); err != nil {
						t.Fatal(err)
					}
				case "recreate":
					if err := c.Delete(ctx, sts); err != nil {
						t.Fatal(err)
					}
				}
				if err := r.reconcileStatefulSet(ctx, fresh, 1); err != nil {
					t.Fatal(err)
				}
				if err := c.Get(ctx, client.ObjectKeyFromObject(cache), sts); err != nil {
					t.Fatal(err)
				}
				expected := fresh.Spec.DeepCopy().Tolerations
				for i := range expected {
					if expected[i].Operator == "" {
						expected[i].Operator = corev1.TolerationOpEqual
					}
				}
				if len(expected) != len(sts.Spec.Template.Spec.Tolerations) ||
					(len(expected) > 0 && !reflect.DeepEqual(expected, sts.Spec.Template.Spec.Tolerations)) {
					t.Fatalf("%s: got %#v, want %#v", action, sts.Spec.Template.Spec.Tolerations, expected)
				}
				if sts.Spec.UpdateStrategy.Type != appsv1.RollingUpdateStatefulSetStrategyType {
					t.Fatal("toleration changes must use existing rolling updates")
				}
			}
		})
	}
	cache.Spec.Tolerations = []corev1.Toleration{{Key: "sku", Operator: "Invalid"}}
	if err := r.reconcileStatefulSet(ctx, cache, 1); err == nil || !strings.Contains(err.Error(), "spec.tolerations[0].operator") {
		t.Fatalf("invalid tolerations must fail closed without admission: %v", err)
	}
}
