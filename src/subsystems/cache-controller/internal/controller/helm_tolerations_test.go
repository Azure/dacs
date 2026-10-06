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
	"bytes"
	"io"
	"os/exec"
	"reflect"
	"testing"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/apis/meta/v1/unstructured"
	"k8s.io/apimachinery/pkg/runtime"
	yamlutil "k8s.io/apimachinery/pkg/util/yaml"
)

func TestHelmCacheTolerations(t *testing.T) {
	helm, err := exec.LookPath("helm")
	if err != nil {
		t.Skip("helm is required for chart rendering tests")
	}
	gpu := []corev1.Toleration{{Key: "sku", Operator: corev1.TolerationOpEqual, Value: "gpu", Effect: corev1.TaintEffectNoSchedule}}
	controllerDefault := []corev1.Toleration{{Key: "CriticalAddonsOnly", Operator: corev1.TolerationOpExists}}
	customController := []corev1.Toleration{{Key: "controller-only", Operator: corev1.TolerationOpExists}}
	for _, tt := range []struct {
		name    string
		args    []string
		cache   []corev1.Toleration
		manager []corev1.Toleration
		invalid bool
	}{
		{name: "defaults", manager: controllerDefault},
		{name: "gpu", args: []string{"--set-json", `cache.tolerations=[{"key":"sku","operator":"Equal","value":"gpu","effect":"NoSchedule"}]`}, cache: gpu, manager: controllerDefault},
		{name: "independent controller", args: []string{"--set-json", `manager.tolerations=[{"key":"controller-only","operator":"Exists"}]`}, manager: customController},
		{name: "empty", args: []string{"--set-json", "cache.tolerations=[]"}, manager: controllerDefault},
		{name: "invalid operator", args: []string{"--set-json", `cache.tolerations=[{"key":"sku","operator":"Invalid"}]`}, invalid: true},
		{name: "exists with value", args: []string{"--set-json", `cache.tolerations=[{"operator":"Exists","value":"gpu"}]`}, invalid: true},
		{name: "empty key", args: []string{"--set-json", `cache.tolerations=[{"operator":"Equal","value":"gpu"}]`}, invalid: true},
		{name: "invalid effect", args: []string{"--set-json", `cache.tolerations=[{"key":"sku","effect":"Invalid"}]`}, invalid: true},
		{name: "seconds without NoExecute", args: []string{"--set-json", `cache.tolerations=[{"key":"sku","tolerationSeconds":30}]`}, invalid: true},
	} {
		t.Run(tt.name, func(t *testing.T) {
			args := append([]string{"template", "tolerations", "../../charts/dacs", "--set", "cache.enabled=true"}, tt.args...)
			output, err := exec.Command(helm, args...).CombinedOutput()
			if tt.invalid {
				if err == nil {
					t.Fatal("invalid tolerations passed chart schema validation")
				}
				if !bytes.Contains(output, []byte("cache.tolerations")) && !bytes.Contains(output, []byte("/cache/tolerations")) {
					t.Fatalf("chart failed for an unrelated reason: %s", output)
				}
				return
			}
			if err != nil {
				t.Fatalf("helm template: %v\n%s", err, output)
			}
			decoder := yamlutil.NewYAMLOrJSONDecoder(bytes.NewReader(output), 4096)
			foundCache, foundManager := false, false
			for {
				obj := &unstructured.Unstructured{}
				if err := decoder.Decode(obj); err == io.EOF {
					break
				} else if err != nil {
					t.Fatal(err)
				}
				switch obj.GetKind() {
				case "Cache":
					foundCache = true
					cache := &storagev1.Cache{}
					if err := runtime.DefaultUnstructuredConverter.FromUnstructured(obj.Object, cache); err != nil {
						t.Fatal(err)
					}
					if !reflect.DeepEqual(cache.Spec.Tolerations, tt.cache) {
						t.Fatalf("cache tolerations: got %#v, want %#v", cache.Spec.Tolerations, tt.cache)
					}
				case "Deployment":
					foundManager = true
					deployment := &appsv1.Deployment{}
					if err := runtime.DefaultUnstructuredConverter.FromUnstructured(obj.Object, deployment); err != nil {
						t.Fatal(err)
					}
					if !reflect.DeepEqual(deployment.Spec.Template.Spec.Tolerations, tt.manager) {
						t.Fatalf("controller tolerations changed: got %#v, want %#v", deployment.Spec.Template.Spec.Tolerations, tt.manager)
					}
				}
			}
			if !foundCache || !foundManager {
				t.Fatal("rendered chart is missing Cache or controller Deployment")
			}
		})
	}
}
