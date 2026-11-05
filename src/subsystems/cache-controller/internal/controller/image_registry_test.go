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
	"os"
	"path/filepath"
	"reflect"
	"testing"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/cache-controller/internal/config"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/types"
	"sigs.k8s.io/controller-runtime/pkg/client/fake"
)

func TestRenderTemplate_ImagePullSecrets(t *testing.T) {
	for _, secrets := range [][]corev1.LocalObjectReference{
		nil,
		{},
		{{Name: "registry-auth"}},
		{{Name: "registry-auth"}, {Name: "backup"}},
	} {
		values := baseRenderValues()
		values.ImagePullSecrets = secrets
		var sts appsv1.StatefulSet
		if err := renderTemplateInto("templates/cacheserver-app.yaml", values, &sts); err != nil {
			t.Fatal(err)
		}
		got := sts.Spec.Template.Spec.ImagePullSecrets
		if len(got) != len(secrets) || (len(got) > 0 && !reflect.DeepEqual(got, secrets)) {
			t.Fatalf("rendered pull secrets = %#v, want %#v", got, secrets)
		}
	}
}

func TestReconcileImageRegistry(t *testing.T) {
	file := filepath.Join(t.TempDir(), "config.yaml")
	if err := os.WriteFile(file, []byte("cacheServerImage: test-cache-server:latest\n"), 0600); err != nil {
		t.Fatal(err)
	}

	config.InitConfig(file)
	cfg := config.GetConfig()
	original := *cfg
	t.Cleanup(func() { *cfg = original })
	scheme := runtime.NewScheme()
	for _, add := range []func(*runtime.Scheme) error{storagev1.AddToScheme, appsv1.AddToScheme} {
		if err := add(scheme); err != nil {
			t.Fatal(err)
		}
	}
	ctx := context.Background()
	cache := &storagev1.Cache{ObjectMeta: metav1.ObjectMeta{Name: "private-cache", Namespace: "tenant"}}
	if err := cache.Default(ctx, cache); err != nil {
		t.Fatal(err)
	}
	cache.Spec.Tolerations = []corev1.Toleration{{
		Key: "sku", Operator: corev1.TolerationOpEqual, Value: "gpu", Effect: corev1.TaintEffectNoSchedule,
	}}
	c := fake.NewClientBuilder().WithScheme(scheme).WithObjects(cache).Build()
	r := &CacheReconciler{Client: c, Scheme: scheme}

	// Reconcile the same Cache repeatedly: upgrades must replace both the
	// image and credentials, and removing the setting must remove old secrets.
	for _, tc := range []struct {
		image   string
		secrets []string
	}{
		{"mirror.azurecr.cn/cache-server:release", []string{"china-acr"}},
		{"harbor.example:8443/project/cache-server:release", []string{"harbor", "backup"}},
		{"cache-server:local", nil},
	} {
		t.Run(tc.image, func(t *testing.T) {
			cfg.CacheServerImage = tc.image
			cfg.ImagePullSecrets = tc.secrets
			cfg.ImagePullPolicy = corev1.PullIfNotPresent
			if err := r.reconcileStatefulSet(ctx, cache, 1); err != nil {
				t.Fatal(err)
			}
			var sts appsv1.StatefulSet
			if err := c.Get(ctx, types.NamespacedName{Name: cache.Name, Namespace: cache.Namespace}, &sts); err != nil {
				t.Fatal(err)
			}
			pod := sts.Spec.Template.Spec
			if !reflect.DeepEqual(pod.Tolerations, cache.Spec.Tolerations) {
				t.Fatalf("registry reconciliation changed tolerations: got %v, want %v", pod.Tolerations, cache.Spec.Tolerations)
			}
			if pod.Containers[0].Image != tc.image {
				t.Fatalf("image = %q, want %q", pod.Containers[0].Image, tc.image)
			}
			var names []string
			for _, secret := range pod.ImagePullSecrets {
				names = append(names, secret.Name)
			}
			if !reflect.DeepEqual(names, tc.secrets) {
				t.Fatalf("pull secrets = %v, want %v", names, tc.secrets)
			}
		})
	}
}
