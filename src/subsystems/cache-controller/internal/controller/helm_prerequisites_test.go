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
	"strings"
	"testing"

	appsv1 "k8s.io/api/apps/v1"
	batchv1 "k8s.io/api/batch/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/apis/meta/v1/unstructured"
	"k8s.io/apimachinery/pkg/runtime"
	yamlutil "k8s.io/apimachinery/pkg/util/yaml"
)

func TestHelmPrerequisites(t *testing.T) {
	helm, err := exec.LookPath("helm")
	if err != nil {
		t.Skip("helm is required for chart rendering tests")
	}
	if output, err := exec.Command(helm, "dependency", "build", "../../charts/dacs", "--skip-refresh").CombinedOutput(); err != nil {
		t.Fatalf("building chart dependencies: %v\n%s", err, output)
	}
	controllerDefault := []corev1.Toleration{{Key: "CriticalAddonsOnly", Operator: corev1.TolerationOpExists}}
	customController := []corev1.Toleration{{Key: "controller-only", Operator: corev1.TolerationOpExists}}
	for _, tt := range []struct {
		name            string
		args            []string
		manager         []corev1.Toleration
		prereq          bool
		global          string
		localized       bool
		pullSecrets     []corev1.LocalObjectReference
		invalidProperty string
	}{
		{name: "defaults", manager: controllerDefault},
		{name: "independent controller", args: []string{"--set-json", `manager.tolerations=[{"key":"controller-only","operator":"Exists"}]`}, manager: customController},
		{name: "bundled prerequisites", manager: controllerDefault, prereq: true},
		{name: "legacy prerequisite registry", manager: controllerDefault, prereq: true, global: `{"registry":"mirror.example.com:5000/project/","imagePullSecrets":[{"name":"registry-auth"}]}`, pullSecrets: []corev1.LocalObjectReference{{Name: "registry-auth"}}},
		{name: "private prerequisites", manager: controllerDefault, prereq: true, global: `{"registry":"mirror.example.com:5000/project/","registryMode":"replace","imagePullSecrets":[{"name":"registry-auth"}]}`, localized: true, pullSecrets: []corev1.LocalObjectReference{{Name: "registry-auth"}}},
		{name: "private prerequisites without pull secrets", manager: controllerDefault, prereq: true, global: `{"registry":"mirror.example.com:5000/project/","registryMode":"replace","imagePullSecrets":[]}`, localized: true},
		{name: "invalid prerequisite registry", prereq: true, global: `{"registry":"https://mirror.example.com"}`, invalidProperty: "global.registry"},
		{name: "invalid prerequisite registry mode", prereq: true, global: `{"registryMode":"invalid"}`, invalidProperty: "global.registryMode"},
		{name: "invalid prerequisite pull secret", prereq: true, global: `{"imagePullSecrets":[{"name":""}]}`, invalidProperty: "global.imagePullSecrets"},
	} {
		t.Run(tt.name, func(t *testing.T) {
			args := []string{"template", "prerequisites", "../../charts/dacs"}
			if tt.prereq {
				args = append(args, "--set", "cacheServerPrereq.enabled=true",
					"--set", "cacheServerPrereq.nvmeRaidSetup.image.repository=example.azurecr.io/cache-setup",
					"--set-string", "cacheServerPrereq.nvmeRaidSetup.image.tag=20261016.1")
			}
			if tt.global != "" {
				args = append(args, "--set-json", "global="+tt.global)
			}
			args = append(args, tt.args...)
			output, err := exec.Command(helm, args...).CombinedOutput()
			if tt.invalidProperty != "" {
				if err == nil {
					t.Fatal("invalid values passed chart schema validation")
				}
				property := tt.invalidProperty
				if !bytes.Contains(output, []byte(property)) && !bytes.Contains(output, []byte("/"+strings.ReplaceAll(property, ".", "/"))) {
					t.Fatalf("chart failed for an unrelated reason: %s", output)
				}
				return
			}
			if err != nil {
				t.Fatalf("helm template: %v\n%s", err, output)
			}
			decoder := yamlutil.NewYAMLOrJSONDecoder(bytes.NewReader(output), 4096)
			foundManager := false
			foundPrereq, foundRollout := false, false
			setupImage := "example.azurecr.io/cache-setup:20261016.1"
			pauseImage := "mcr.microsoft.com/azurelinux/base/core:3.0"
			rolloutImage := "mcr.microsoft.com/oss/v2/kubernetes/kubectl:v1.35.9"
			if tt.localized {
				setupImage = "mirror.example.com:5000/project/cache-setup:20261016.1"
				pauseImage = "mirror.example.com:5000/project/azurelinux/base/core:3.0"
				rolloutImage = "mirror.example.com:5000/project/oss/v2/kubernetes/kubectl:v1.35.9"
			}
			for {
				obj := &unstructured.Unstructured{}
				if err := decoder.Decode(obj); err == io.EOF {
					break
				} else if err != nil {
					t.Fatal(err)
				}
				switch obj.GetKind() {
				case "DaemonSet":
					foundPrereq = true
					var daemonset appsv1.DaemonSet
					if err := runtime.DefaultUnstructuredConverter.FromUnstructured(obj.Object, &daemonset); err != nil {
						t.Fatal(err)
					}
					init := daemonset.Spec.Template.Spec.InitContainers
					if len(init) != 1 || init[0].Image != setupImage {
						t.Fatalf("prerequisite init containers: %#v", init)
					}
					if !daemonset.Spec.Template.Spec.HostPID {
						t.Fatal("NVMe setup requires host PID access")
					}
					pod := daemonset.Spec.Template.Spec
					if len(pod.Containers) != 1 || pod.Containers[0].Image != pauseImage {
						t.Fatalf("prerequisite pause containers: %#v", pod.Containers)
					}
					if !reflect.DeepEqual(pod.ImagePullSecrets, tt.pullSecrets) {
						t.Fatalf("prerequisite pull secrets: got %#v, want %#v", pod.ImagePullSecrets, tt.pullSecrets)
					}
				case "Job":
					if obj.GetAnnotations()["helm.sh/hook"] != "post-install,post-upgrade" {
						continue
					}
					foundRollout = true
					var job batchv1.Job
					if err := runtime.DefaultUnstructuredConverter.FromUnstructured(obj.Object, &job); err != nil {
						t.Fatal(err)
					}
					if job.Spec.ActiveDeadlineSeconds == nil || *job.Spec.ActiveDeadlineSeconds != 780 {
						t.Fatalf("rollout-check deadline: %v", job.Spec.ActiveDeadlineSeconds)
					}
					pod := job.Spec.Template.Spec
					if len(pod.Containers) != 1 || pod.Containers[0].Image != rolloutImage {
						t.Fatalf("rollout-check containers: %#v", pod.Containers)
					}
					if strings.Contains(strings.Join(pod.Containers[0].Command, "\n"), "--request-timeout") {
						t.Fatal("rollout check must use process timeouts to preserve in-cluster authentication")
					}
					if !reflect.DeepEqual(pod.ImagePullSecrets, tt.pullSecrets) {
						t.Fatalf("rollout-check pull secrets: got %#v, want %#v", pod.ImagePullSecrets, tt.pullSecrets)
					}
				case "Cache":
					t.Fatal("controller chart must not create Cache resources")
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
			if !foundManager {
				t.Fatal("rendered chart is missing controller Deployment")
			}
			if foundPrereq != tt.prereq || foundRollout != tt.prereq {
				t.Fatalf("bundled prerequisites: daemonset=%t, rollout=%t, want %t", foundPrereq, foundRollout, tt.prereq)
			}
		})
	}
}
