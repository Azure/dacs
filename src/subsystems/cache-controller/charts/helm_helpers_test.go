//go:build charttests

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

package charts

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"k8s.io/apimachinery/pkg/apis/meta/v1/unstructured"
	yamlutil "k8s.io/apimachinery/pkg/util/yaml"
)

func TestHelmHelperIsolation(t *testing.T) {
	helm, err := exec.LookPath("helm")
	if err != nil {
		t.Fatal("helm is required for chart rendering tests")
	}
	if output, err := exec.Command(helm, "dependency", "build", "dacs", "--skip-refresh").CombinedOutput(); err != nil {
		t.Fatalf("building chart dependencies: %v\n%s", err, output)
	}
	render := func(t *testing.T, chart string, prerequisites bool) map[string]map[string]interface{} {
		t.Helper()
		prefix := ""
		if chart != "dacs" {
			prefix = "dacs."
		}
		args := []string{"template", "isolation", chart, "--namespace", "dacs-system",
			"--set", fmt.Sprintf("%scacheServerPrereq.enabled=%t", prefix, prerequisites)}
		if prerequisites {
			args = append(args,
				"--set", prefix+"cacheServerPrereq.nvmeRaidSetup.image.repository=example.azurecr.io/cache-setup",
				"--set-string", prefix+"cacheServerPrereq.nvmeRaidSetup.image.tag=20261016.1")
		}
		output, err := exec.Command(helm, args...).CombinedOutput()
		if err != nil {
			t.Fatalf("rendering %s: %v\n%s", chart, err, output)
		}
		resources := make(map[string]map[string]interface{})
		decoder := yamlutil.NewYAMLOrJSONDecoder(bytes.NewReader(output), 4096)
		for {
			obj := &unstructured.Unstructured{}
			if err := decoder.Decode(obj); err == io.EOF {
				break
			} else if err != nil {
				t.Fatal(err)
			}
			if obj.GetKind() != "" {
				resources[obj.GetAPIVersion()+"/"+obj.GetKind()+"/"+obj.GetNamespace()+"/"+obj.GetName()] = obj.Object
			}
		}
		return resources
	}
	write := func(path, contents string) {
		t.Helper()
		if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte(contents), 0644); err != nil {
			t.Fatal(err)
		}
	}
	for _, prerequisites := range []bool{false, true} {
		standalone := render(t, "dacs", prerequisites)
		for _, peer := range []string{"aaa-peer", "zzz-peer"} {
			t.Run(fmt.Sprintf("%s/prerequisites=%t", peer, prerequisites), func(t *testing.T) {
				umbrella := t.TempDir()
				if err := os.CopyFS(filepath.Join(umbrella, "charts", "dacs"), os.DirFS("dacs")); err != nil {
					t.Fatal(err)
				}
				write(filepath.Join(umbrella, "Chart.yaml"), fmt.Sprintf(`apiVersion: v2
name: umbrella
version: 0.1.0
dependencies:
  - name: dacs
    version: 0.1.0
  - name: %s
    version: 0.1.0
`, peer))
				peerPath := filepath.Join(umbrella, "charts", peer)
				write(filepath.Join(peerPath, "Chart.yaml"), "apiVersion: v2\nname: "+peer+"\nversion: 0.1.0\n")
				var helpers, resource strings.Builder
				resource.WriteString("apiVersion: v1\nkind: ConfigMap\nmetadata:\n  name: peer\ndata:\n")
				for _, name := range []string{"name", "fullname", "namespaceName", "serviceName", "labels", "selectorLabels", "serviceAccountName", "priorityClassName", "image"} {
					fmt.Fprintf(&helpers, "{{- define \"chart.%s\" -}}peer{{- end -}}\n", name)
					fmt.Fprintf(&resource, "  %s: {{ include \"chart.%s\" . | quote }}\n", name, name)
				}
				write(filepath.Join(peerPath, "templates", "_helpers.tpl"), helpers.String())
				write(filepath.Join(peerPath, "templates", "peer.yaml"), resource.String())
				combined := render(t, umbrella, prerequisites)
				peerKey := "v1/ConfigMap//peer"
				peerResource, ok := combined[peerKey]
				if !ok {
					t.Fatal("sibling chart's ConfigMap is missing")
				}
				values, _, err := unstructured.NestedStringMap(peerResource, "data")
				if err != nil {
					t.Fatal(err)
				}
				for name, value := range values {
					if value != "peer" {
						t.Errorf("DACS replaced sibling helper chart.%s: %q", name, value)
					}
				}
				delete(combined, peerKey)
				if !reflect.DeepEqual(standalone, combined) {
					for key := range combined {
						if _, ok := standalone[key]; !ok {
							t.Errorf("extra umbrella resource: %s", key)
						}
					}
					for key, resource := range standalone {
						if !reflect.DeepEqual(resource, combined[key]) {
							t.Errorf("resource differs: %s", key)
						}
					}
					t.Fatal("DACS resources differ between standalone and umbrella rendering")
				}
			})
		}
	}
}
