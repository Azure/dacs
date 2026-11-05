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
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"reflect"
	"slices"
	"strings"
	"testing"

	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/apis/meta/v1/unstructured"
	"k8s.io/apimachinery/pkg/util/yaml"
	sigsyaml "sigs.k8s.io/yaml"
)

type registryChart struct {
	path              string
	defaults          []string
	images            []string
	legacyPrefixCount int
}

func registryCharts(t *testing.T) []registryChart {
	t.Helper()
	prereq := filepath.Join("..", "..", "..", "..", "helm", "dacs-prereq")
	return []registryChart{
		{"dacs", []string{"dacs/cache-controller:local", "dacs/cache-server:local", "mcr.microsoft.com/oss/v2/kubernetes/kubectl:" + chartImageTag(t, "dacs", "cleanup", false)},
			[]string{"manager.image", "manager.cacheServerImage", "cleanup.image"}, 2},
		{prereq, []string{"cache-setup:1.0.0", "mcr.microsoft.com/azurelinux/base/core:3.0", "mcr.microsoft.com/oss/v2/kubernetes/kubectl:" + chartImageTag(t, prereq, "rolloutCheck", true)},
			[]string{"nvmeRaidSetup.image", "pauseContainer.image", "rolloutCheck.image"}, 0},
		{filepath.Join("..", "..", "..", "..", "helm", "cache-server"), []string{"acstor/dacs-scheduler:1.16.0", "acstor/dacs-server:1.16.0"},
			[]string{"cacheServer.scheduler.image", "cacheServer.image"}, 0},
	}
}

func chartImageTag(t *testing.T, chart, component string, fallbackToAppVersion bool) string {
	t.Helper()
	data, err := os.ReadFile(filepath.Join(chart, "values.yaml"))
	if err != nil {
		t.Fatal(err)
	}
	var values map[string]interface{}
	if err := sigsyaml.Unmarshal(data, &values); err != nil {
		t.Fatal(err)
	}
	tag, _, err := unstructured.NestedString(values, component, "image", "tag")
	if err != nil {
		t.Fatal(err)
	}
	if tag != "" || !fallbackToAppVersion {
		return tag
	}
	data, err = os.ReadFile(filepath.Join(chart, "Chart.yaml"))
	if err != nil {
		t.Fatal(err)
	}
	var metadata struct {
		AppVersion string
	}
	if err := sigsyaml.Unmarshal(data, &metadata); err != nil {
		t.Fatal(err)
	}
	return metadata.AppVersion
}

func renderPods(t *testing.T, chart string, settings ...string) []corev1.PodSpec {
	t.Helper()
	var args []string
	for _, setting := range settings {
		args = append(args, "--set-string", setting)
	}
	return renderPodsWithArgs(t, chart, args...)
}

func renderPodsWithArgs(t *testing.T, chart string, options ...string) []corev1.PodSpec {
	t.Helper()
	args := append([]string{"template", "registry-test", chart}, options...)
	out, err := exec.Command("helm", args...).CombinedOutput()
	if err != nil {
		t.Fatalf("helm %v: %v\n%s", args, err, out)
	}
	decoder := yaml.NewYAMLOrJSONDecoder(bytes.NewReader(out), 4096)
	var pods []corev1.PodSpec
	for {
		var obj struct {
			Kind string
			Spec struct {
				Template struct {
					Spec corev1.PodSpec
				}
			}
		}
		if err := decoder.Decode(&obj); err == io.EOF {
			break
		} else if err != nil {
			t.Fatal(err)
		}
		switch obj.Kind {
		case "Deployment", "StatefulSet", "DaemonSet", "Job":
			pods = append(pods, obj.Spec.Template.Spec)
		}
	}
	if len(pods) == 0 {
		t.Fatal("no workloads rendered")
	}
	return pods
}

func podImages(pods []corev1.PodSpec) []string {
	var images []string
	for _, pod := range pods {
		for _, c := range append(pod.InitContainers, pod.Containers...) {
			images = append(images, c.Image)
			for _, env := range c.Env {
				if env.Name == "CACHE_CONTROLLER_CACHESERVERIMAGE" {
					images = append(images, env.Value)
				}
			}
		}
	}
	return images
}

func checkLegacyRegistry(t *testing.T, chart string, original []string, prefixCount int) {
	t.Helper()
	for _, registry := range []string{"mirror/archive", "harbor.example:8443/dacs/"} {
		want := append([]string{}, original...)
		for i := 0; i < prefixCount; i++ {
			want[i] = registry + "/" + want[i]
		}
		for _, mode := range []string{"", "legacy"} {
			settings := []string{"global.registry=" + registry, "global.imagePullSecrets[0].name=registry-auth"}
			if mode != "" {
				settings = append(settings, "global.registryMode="+mode)
			}
			pods := renderPods(t, chart, settings...)
			if got := podImages(pods); !reflect.DeepEqual(got, want) {
				t.Fatalf("legacy images (mode %q) = %v, want %v", mode, got, want)
			}
			for _, pod := range pods {
				if !reflect.DeepEqual(pod.ImagePullSecrets, []corev1.LocalObjectReference{{Name: "registry-auth"}}) {
					t.Fatalf("legacy pull secrets = %v", pod.ImagePullSecrets)
				}
			}
		}
	}
}

func TestImageRegistryReusedValues(t *testing.T) {
	for _, chart := range registryCharts(t) {
		t.Run(filepath.Base(chart.path), func(t *testing.T) {
			staging := t.TempDir()
			if err := os.CopyFS(staging, os.DirFS(chart.path)); err != nil {
				t.Fatal(err)
			}
			valuesPath := filepath.Join(staging, "values.yaml")
			data, err := os.ReadFile(valuesPath)
			if err != nil {
				t.Fatal(err)
			}
			var values map[string]interface{}
			if err := sigsyaml.Unmarshal(data, &values); err != nil {
				t.Fatal(err)
			}
			// --reuse-values replaces new chart defaults with the old computed
			// values, which predate the global map. A -f override would merge it back.
			delete(values, "global")
			for _, image := range chart.images {
				object := values
				for _, key := range strings.Split(image, ".") {
					child, ok := object[key].(map[string]interface{})
					if !ok {
						t.Fatalf("missing image object %s in chart defaults", image)
					}
					object = child
				}
				delete(object, "registry")
			}
			data, err = sigsyaml.Marshal(values)
			if err != nil {
				t.Fatal(err)
			}
			if err := os.WriteFile(valuesPath, data, 0600); err != nil {
				t.Fatal(err)
			}
			pods := renderPods(t, staging)
			if got := podImages(pods); !reflect.DeepEqual(got, chart.defaults) {
				t.Fatalf("reused defaults changed images: got %v, want %v", got, chart.defaults)
			}
			for _, pod := range pods {
				if len(pod.ImagePullSecrets) != 0 {
					t.Fatal("reused defaults unexpectedly require pull secrets")
				}
				for _, container := range pod.Containers {
					for _, env := range container.Env {
						if env.Name == "CACHE_CONTROLLER_IMAGEPULLSECRETS" && env.Value != "" {
							t.Fatalf("reused defaults propagated pull secrets: %q", env.Value)
						}
					}
				}
			}
			// An old user-supplied registry has no mode or pull-secret fields.
			want := append([]string{}, chart.defaults...)
			for i := 0; i < chart.legacyPrefixCount; i++ {
				want[i] = "mirror/archive/" + want[i]
			}
			if got := podImages(renderPods(t, staging, "global.registry=mirror/archive")); !reflect.DeepEqual(got, want) {
				t.Fatalf("reused registry changed images: got %v, want %v", got, want)
			}
		})
	}
}

func TestImageRegistryNumericTags(t *testing.T) {
	for _, chart := range registryCharts(t) {
		t.Run(filepath.Base(chart.path), func(t *testing.T) {
			data, err := os.ReadFile(filepath.Join(chart.path, "Chart.yaml"))
			if err != nil {
				t.Fatal(err)
			}
			var metadata struct {
				AppVersion string `json:"appVersion"`
			}
			if err := sigsyaml.Unmarshal(data, &metadata); err != nil {
				t.Fatal(err)
			}
			for i, image := range chart.images {
				if i < chart.legacyPrefixCount {
					continue // These tags already require strings in the chart schema.
				}
				t.Run(image, func(t *testing.T) {
					repository := chart.defaults[i][:strings.LastIndex(chart.defaults[i], ":")]
					for _, tag := range []string{"123", "0"} {
						want := repository + ":" + tag
						if tag == "0" && image != "cleanup.image" && image != "pauseContainer.image" {
							want = repository + ":" + metadata.AppVersion
						}
						for _, mode := range []string{"legacy", "replace"} {
							args := []string{"--set", image + ".tag=" + tag, "--set-string", "global.registryMode=" + mode}
							expected := want
							if mode == "replace" {
								args = append(args, "--set-string", "global.registry=mirror.example")
								expected = "mirror.example/" + strings.TrimPrefix(want, "mcr.microsoft.com/")
							}
							if got := podImages(renderPodsWithArgs(t, chart.path, args...))[i]; got != expected {
								t.Fatalf("numeric tag %s in %s mode: got %q, want %q", tag, mode, got, expected)
							}
						}
					}
				})
			}
		})
	}
}

// Run the real publisher on an isolated source copy; never push or edit the checkout.
func TestImageRegistryPackagedCharts(t *testing.T) {
	root, err := filepath.Abs(filepath.Join("..", "..", "..", ".."))
	if err != nil {
		t.Fatal(err)
	}
	staging := t.TempDir()
	paths := []string{".pipelines/scripts/publish/publish-helm-chart.sh", ".pipelines/scripts/get-version.sh"}
	for _, chart := range registryCharts(t) {
		chartPath, err := filepath.Abs(chart.path)
		if err != nil {
			t.Fatal(err)
		}
		rel, err := filepath.Rel(root, chartPath)
		if err != nil {
			t.Fatal(err)
		}
		paths = append(paths, rel)
	}
	originals := map[string][]byte{}
	for _, path := range paths {
		err := filepath.WalkDir(filepath.Join(root, path), func(source string, entry fs.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if entry.Name() == "charts" && entry.IsDir() {
				return filepath.SkipDir
			}
			if entry.Name() == "Chart.lock" {
				return nil
			}
			rel, err := filepath.Rel(root, source)
			if err != nil {
				return err
			}
			target := filepath.Join(staging, rel)
			if entry.IsDir() {
				return os.MkdirAll(target, 0755)
			}
			data, err := os.ReadFile(source)
			if err != nil {
				return err
			}
			originals[target] = data
			if err := os.MkdirAll(filepath.Dir(target), 0755); err != nil {
				return err
			}
			info, err := entry.Info()
			if err != nil {
				return err
			}
			return os.WriteFile(target, data, info.Mode().Perm())
		})
		if err != nil {
			t.Fatal(err)
		}
	}
	cmd := exec.Command("bash", filepath.Join(staging, paths[0]))
	cmd.Env = append(os.Environ(), "DOCKER_REGISTRY=registry.example.com/team", "DACS_REPOSITORY_ROOT="+root)
	version, err := exec.Command(filepath.Join(root, paths[1]), "container").Output()
	if err != nil {
		t.Fatal(err)
	}
	containerVersion := strings.TrimSpace(string(version))
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("publish charts: %v\n%s", err, out)
	}
	for path, want := range originals {
		got, err := os.ReadFile(path)
		if err != nil || !bytes.Equal(got, want) {
			t.Fatalf("publisher failed to restore %s: %v", path, err)
		}
	}
	for _, chart := range registryCharts(t) {
		if filepath.Base(chart.path) == "dacs-prereq" {
			continue // Prerequisites are also tested through their parent charts.
		}
		t.Run(filepath.Base(chart.path), func(t *testing.T) {
			packages, err := filepath.Glob(filepath.Join(staging, "dist", filepath.Base(chart.path)+"-[0-9]*.tgz"))
			if err != nil || len(packages) != 1 {
				t.Fatalf("expected one chart package: %v, %v", packages, err)
			}
			members, err := exec.Command("tar", "-tzf", packages[0]).CombinedOutput()
			if err != nil {
				t.Fatalf("list packaged chart: %v\n%s", err, members)
			}
			for _, member := range strings.Split(string(members), "\n") {
				if strings.HasPrefix(member, filepath.Base(chart.path)+"/tests/") {
					t.Fatalf("unit test suite was included in published chart: %s", member)
				}
			}
			var published []string
			for _, image := range chart.defaults {
				if strings.HasPrefix(image, "mcr.microsoft.com/") {
					published = append(published, image)
				} else {
					repository := strings.Split(image, ":")[0]
					repository = strings.ReplaceAll(repository, "acstor/dacs-scheduler", "dacs/cacheserver-scheduler")
					repository = strings.ReplaceAll(repository, "acstor/dacs-server", "dacs/cache-server")
					published = append(published, "registry.example.com/team/"+repository+":"+containerVersion)
				}
			}
			if got := podImages(renderPods(t, packages[0])); !reflect.DeepEqual(got, published) {
				t.Fatalf("published defaults = %v, want %v", got, published)
			}
			checkLegacyRegistry(t, packages[0], published, chart.legacyPrefixCount)
			prereq := registryCharts(t)[1]
			for i, image := range prereq.images {
				for _, mode := range []string{"legacy", "replace"} {
					pods := renderPodsWithArgs(t, packages[0], "--set", "cacheServerPrereq.enabled=true",
						"--set-string", "global.registry=mirror.example", "--set-string", "global.registryMode="+mode,
						"--set-string", "global.imagePullSecrets[0].name=registry-auth",
						"--set-string", "cacheServerPrereq."+image+".registry=component.example/project")
					want := append([]string{}, published...)
					want = append(want, "registry.example.com/team/dacs/cache-setup:"+containerVersion,
						prereq.defaults[1], prereq.defaults[2])
					for j, source := range want {
						if j == len(published)+i {
							want[j] = "component.example/project/" + strings.SplitN(source, "/", 2)[1]
						} else if mode == "replace" {
							want[j] = "mirror.example/" + strings.SplitN(source, "/", 2)[1]
						} else if j < chart.legacyPrefixCount {
							want[j] = "mirror.example/" + source
						}
					}
					got := podImages(pods)
					slices.Sort(got)
					slices.Sort(want)
					if !reflect.DeepEqual(got, want) {
						t.Fatalf("bundled images (%s, %s) = %v, want %v", image, mode, got, want)
					}
					for _, pod := range pods {
						if !reflect.DeepEqual(pod.ImagePullSecrets, []corev1.LocalObjectReference{{Name: "registry-auth"}}) {
							t.Fatalf("bundled pull secrets = %v", pod.ImagePullSecrets)
						}
					}
				}
			}
			for _, registry := range []string{"mirror.azurecr.cn", "harbor.example:8443/dacs"} {
				pods := renderPods(t, packages[0], "global.registry="+registry, "global.registryMode=replace", "global.imagePullSecrets[0].name=registry-auth")
				for i, got := range podImages(pods) {
					want := registry + "/" + strings.SplitN(published[i], "/", 2)[1]
					if got != want {
						t.Fatalf("packaged override = %q, want %q", got, want)
					}
				}
				for _, pod := range pods {
					if !reflect.DeepEqual(pod.ImagePullSecrets, []corev1.LocalObjectReference{{Name: "registry-auth"}}) {
						t.Fatalf("packaged pull secrets = %v", pod.ImagePullSecrets)
					}
				}
				for i, image := range chart.images {
					got := podImages(renderPods(t, packages[0], "global.registry="+registry, "global.registryMode=replace",
						image+".repository=source.example/custom", image+".tag=override"))[i]
					if got != registry+"/custom:override" {
						t.Fatalf("packaged component override = %q", got)
					}
					for _, mode := range []string{"legacy", "replace"} {
						overridden := podImages(renderPods(t, packages[0], "global.registry="+registry, "global.registryMode="+mode,
							image+".registry=component.example:5000/project/"))
						for j, got := range overridden {
							want := published[j]
							if i == j {
								want = "component.example:5000/project/" + strings.SplitN(published[j], "/", 2)[1]
							} else if mode == "replace" {
								want = registry + "/" + strings.SplitN(published[j], "/", 2)[1]
							} else if j < chart.legacyPrefixCount {
								want = registry + "/" + published[j]
							}
							if got != want {
								t.Fatalf("packaged per-image registry (%s, %s): got %q, want %q", image, mode, got, want)
							}
						}
					}
				}
			}
		})
	}
}
