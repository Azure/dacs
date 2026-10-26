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
	"encoding/json"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"testing"

	batchv1 "k8s.io/api/batch/v1"
	rbacv1 "k8s.io/api/rbac/v1"
	yamlutil "k8s.io/apimachinery/pkg/util/yaml"
)

func TestHelmStandaloneCache(t *testing.T) {
	helm, err := exec.LookPath("helm")
	if err != nil {
		t.Skip("helm is required for chart rendering tests")
	}
	for _, tt := range []struct {
		name    string
		args    []string
		invalid bool
	}{
		{name: "defaults"},
		{name: "legacy disabled", args: []string{"--set", "cache.enabled=false"}},
		{name: "obsolete settings ignored", args: []string{"--set", "cache.serverThreads=-1,cache.memoryLimitMB=1"}},
		{name: "legacy enabled rejected", args: []string{"--set", "cache.enabled=true"}, invalid: true},
	} {
		t.Run(tt.name, func(t *testing.T) {
			args := append([]string{"template", "standalone", "../../charts/dacs"}, tt.args...)
			output, err := exec.Command(helm, args...).CombinedOutput()
			if tt.invalid {
				if err == nil || !bytes.Contains(output, []byte("Migrate any chart-owned Cache")) {
					t.Fatalf("expected Cache ownership migration error, got %v\n%s", err, output)
				}
				return
			}
			if err != nil {
				t.Fatalf("rendering controller-only chart: %v\n%s", err, output)
			}
			if bytes.Contains(output, []byte("\nkind: Cache\n")) || !bytes.Contains(output, []byte("\nkind: Deployment\n")) {
				t.Fatal("chart must render the controller without creating a Cache")
			}
		})
	}
}

func TestHelmCacheCleanup(t *testing.T) {
	helm, err := exec.LookPath("helm")
	if err != nil {
		t.Skip("helm is required for chart rendering tests")
	}
	chart := "../../charts/dacs"
	script, err := os.ReadFile(filepath.Join(chart, "scripts/pre-delete-cache-cleanup.sh"))
	if err != nil {
		t.Fatalf("reading cleanup script: %v", err)
	}
	for _, tt := range []struct {
		name           string
		args           []string
		force          bool
		timeout        string
		invalid        bool
		invalidTimeout bool
	}{
		{name: "default"},
		{name: "explicit false", args: []string{"--set", "cleanup.forceDeleteCaches=false"}},
		{name: "force delete", args: []string{"--set", "cleanup.forceDeleteCaches=true"}, force: true},
		{name: "custom timeout", args: []string{"--set", "cleanup.forceDeleteCaches=true,cleanup.timeoutSeconds=17"}, force: true, timeout: "17"},
		{name: "zero timeout", args: []string{"--set", "cleanup.forceDeleteCaches=true,cleanup.timeoutSeconds=0"}, force: true, timeout: "0"},
		{name: "leading zero timeout", args: []string{"--set", "cleanup.forceDeleteCaches=true", "--set-string", "cleanup.timeoutSeconds=08"}, force: true, timeout: "08", invalidTimeout: true},
		{name: "octal-looking timeout", args: []string{"--set", "cleanup.forceDeleteCaches=true", "--set-string", "cleanup.timeoutSeconds=010"}, force: true, timeout: "010", invalidTimeout: true},
		{name: "reject string flag", args: []string{"--set-string", "cleanup.forceDeleteCaches=false"}, invalid: true},
	} {
		t.Run(tt.name, func(t *testing.T) {
			args := append([]string{"template", "cleanup", chart, "--show-only", "templates/pre-delete-cache-cleanup.yaml"}, tt.args...)
			output, err := exec.Command(helm, args...).CombinedOutput()
			if tt.invalid {
				if err == nil || !bytes.Contains(output, []byte("forceDeleteCaches")) {
					t.Fatalf("expected cleanup flag schema error, got %v\n%s", err, output)
				}
				return
			}
			if err != nil {
				t.Fatalf("rendering cleanup hook: %v\n%s", err, output)
			}
			decoder := yamlutil.NewYAMLOrJSONDecoder(bytes.NewReader(output), 4096)
			var foundJob, foundRole bool
			for {
				var obj struct {
					Kind string `json:"kind"`
				}
				var document json.RawMessage
				if err := decoder.Decode(&document); err == io.EOF {
					break
				} else if err != nil {
					t.Fatalf("decoding cleanup hook: %v", err)
				}
				if len(bytes.TrimSpace(document)) == 0 {
					continue
				}
				if err := json.Unmarshal(document, &obj); err != nil {
					t.Fatalf("decoding resource kind: %v", err)
				}
				switch obj.Kind {
				case "ConfigMap":
					t.Fatal("cleanup hook must not create a script ConfigMap")
				case "ClusterRole":
					foundRole = true
					var role rbacv1.ClusterRole
					if err := json.Unmarshal(document, &role); err != nil {
						t.Fatalf("decoding cleanup role: %v", err)
					}
					wantVerbs := []string{"get", "list"}
					if tt.force {
						wantVerbs = append(wantVerbs, "patch", "delete")
					}
					if !slices.Equal(role.Rules[0].Verbs, wantVerbs) {
						t.Fatalf("cleanup permissions = %v, want %v", role.Rules[0].Verbs, wantVerbs)
					}
				case "Job":
					foundJob = true
					var job batchv1.Job
					if err := json.Unmarshal(document, &job); err != nil {
						t.Fatalf("decoding cleanup job: %v", err)
					}
					container := job.Spec.Template.Spec.Containers[0]
					if !slices.Equal(container.Command, []string{"/bin/sh", "-c", string(script), "pre-delete-cache-cleanup.sh"}) {
						t.Fatal("cleanup command must inline the chart script unchanged and set its argument zero")
					}
					timeout := tt.timeout
					if timeout == "" {
						timeout = "300"
					}
					var wantArgs []string
					if tt.force {
						wantArgs = append(wantArgs, "--force-cache-deletion")
					}
					wantArgs = append(wantArgs, "--timeout-seconds", timeout)
					if !slices.Equal(container.Args, wantArgs) {
						t.Fatalf("cleanup arguments = %v, want %v", container.Args, wantArgs)
					}
					if len(container.VolumeMounts) != 0 || len(job.Spec.Template.Spec.Volumes) != 0 {
						t.Fatal("inline cleanup script must not require volumes or mounts")
					}
					if tt.invalidTimeout {
						cmd := exec.Command(container.Command[0], append(container.Command[1:], container.Args...)...)
						cmd.Env = []string{"PATH=" + t.TempDir()}
						output, err := cmd.CombinedOutput()
						if err == nil || !bytes.Contains(output, []byte("without leading zeros")) {
							t.Fatalf("expected timeout validation error before invoking kubectl, got %v\n%s", err, output)
						}
					}
				}
			}
			if !foundJob || !foundRole {
				t.Fatal("cleanup hook is missing its Job or ClusterRole")
			}
		})
	}
}
