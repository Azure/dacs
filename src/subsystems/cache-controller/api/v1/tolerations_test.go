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

package v1

import (
	"context"
	"reflect"
	"strings"
	"testing"

	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/utils/ptr"
)

func TestCacheTolerationsValidation(t *testing.T) {
	tests := []struct {
		name        string
		tolerations []corev1.Toleration
		invalid     string
	}{
		{name: "omitted"},
		{name: "empty", tolerations: []corev1.Toleration{}},
		{name: "gpu", tolerations: []corev1.Toleration{{Key: "sku", Operator: corev1.TolerationOpEqual, Value: "gpu", Effect: corev1.TaintEffectNoSchedule}}},
		{name: "default operator and all effects", tolerations: []corev1.Toleration{{Key: "sku", Value: "gpu"}}},
		{name: "exists with qualified key", tolerations: []corev1.Toleration{{Key: "example.com/gpu", Operator: corev1.TolerationOpExists, Effect: corev1.TaintEffectPreferNoSchedule}}},
		{name: "all taints", tolerations: []corev1.Toleration{{Operator: corev1.TolerationOpExists}}},
		{name: "finite eviction", tolerations: []corev1.Toleration{{Key: "sku", Effect: corev1.TaintEffectNoExecute, TolerationSeconds: ptr.To(int64(60))}}},
		{name: "immediate eviction", tolerations: []corev1.Toleration{{Key: "sku", Effect: corev1.TaintEffectNoExecute, TolerationSeconds: ptr.To(int64(-1))}}},
		{name: "bad key", tolerations: []corev1.Toleration{{Key: "bad/key/name"}}, invalid: "key"},
		{name: "bad value", tolerations: []corev1.Toleration{{Key: "sku", Value: "gpu\ninjected"}}, invalid: "value"},
		{name: "unsupported operator", tolerations: []corev1.Toleration{{Key: "sku", Operator: "In"}}, invalid: "operator"},
		{name: "empty key equal", tolerations: []corev1.Toleration{{Operator: corev1.TolerationOpEqual}}, invalid: "operator"},
		{name: "empty key default operator", tolerations: []corev1.Toleration{{}}, invalid: "operator"},
		{name: "exists value", tolerations: []corev1.Toleration{{Key: "sku", Operator: corev1.TolerationOpExists, Value: "gpu"}}, invalid: "value"},
		{name: "bad effect", tolerations: []corev1.Toleration{{Key: "sku", Effect: "Never"}}, invalid: "effect"},
		{name: "seconds with NoSchedule", tolerations: []corev1.Toleration{{Key: "sku", Effect: corev1.TaintEffectNoSchedule, TolerationSeconds: ptr.To(int64(0))}}, invalid: "tolerationSeconds"},
		{name: "seconds without effect", tolerations: []corev1.Toleration{{Key: "sku", TolerationSeconds: ptr.To(int64(60))}}, invalid: "tolerationSeconds"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			cache := &Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "cache"},
				Spec:       CacheSpec{ServerSizeInGB: 4, ServerThreads: 8, Tolerations: tt.tolerations},
			}
			if err := cache.Default(context.Background(), cache); err != nil {
				t.Fatal(err)
			}
			if !reflect.DeepEqual(cache.Spec.Tolerations, tt.tolerations) {
				t.Fatal("defaulting changed tolerations")
			}
			for _, update := range []bool{false, true} {
				var err error
				if update {
					_, err = cache.ValidateUpdate(context.Background(), &Cache{}, cache)
				} else {
					_, err = cache.ValidateCreate(context.Background(), cache)
				}
				if tt.invalid == "" {
					if err != nil {
						t.Fatalf("update=%v: unexpected error: %v", update, err)
					}
				} else if err == nil || !strings.Contains(err.Error(), "spec.tolerations[0]."+tt.invalid) {
					t.Fatalf("update=%v: want invalid %s, got %v", update, tt.invalid, err)
				}
			}
		})
	}
}

func TestCacheTolerationsDeepCopy(t *testing.T) {
	cache := &Cache{Spec: CacheSpec{Tolerations: []corev1.Toleration{{
		Key: "sku", Effect: corev1.TaintEffectNoExecute, TolerationSeconds: ptr.To(int64(60)),
	}}}}
	copy := cache.DeepCopy()
	copy.Spec.Tolerations[0].Key = "changed"
	*copy.Spec.Tolerations[0].TolerationSeconds = 120
	if cache.Spec.Tolerations[0].Key != "sku" || *cache.Spec.Tolerations[0].TolerationSeconds != 60 {
		t.Fatal("deepcopy aliases tolerations")
	}
}
