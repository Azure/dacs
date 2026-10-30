/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package controller

import (
	"strings"
	"testing"

	appsv1 "k8s.io/api/apps/v1"
	"k8s.io/utils/ptr"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
)

// renderValues mirrors the anonymous appValues struct built in
// reconcileStatefulSet. Only field names must match, because the template is
// executed against interface{}.
type renderValues struct {
	Namespace                    string
	NumServers                   int32
	ManagedById                  string
	EgressBandwidthInGb          *int32
	UseEphemeralStorage          bool
	NodeSelectorLabelKey         string
	NodeSelectorValue            []string
	Image                        string
	ImagePullPolicy              string
	ServerThreads                int32
	Port                         int
	MetricsPort                  int
	EnableAccessTimeEviction     bool
	EvictionLastAccessThreshold  string
	EvictionSizeThreshold        string
	EvictionPassFrequency        string
	EvictionMaintainFreeSpace    bool
	EvictionSizeTarget           string
	EvictionBucketInterval       string
	InfinibandResourceLimitCount int32
	InfinibandResourceLimitName  string
	MemoryLimitMB                *int32
	CpuLimit                     *int32
	DisableMemoryRequest         bool
	DisableCpuRequest            bool
	MountSsdPath                 string
	Name                         string
	ServerSizeInGB               int32
	NodeCacheFolderPath          string
	UseHostPath                  bool
	SnapshotIntervalSeconds      int32
	UseZeroCopy                  bool
	UseFlatRadixLocatorIndex     bool
}

func baseRenderValues() renderValues {
	return renderValues{
		Namespace:                    "tenant-a",
		NumServers:                   1,
		NodeSelectorLabelKey:         "dacs.azure.com/processing-unit",
		NodeSelectorValue:            []string{"gpu"},
		Image:                        "img:1",
		ImagePullPolicy:              "IfNotPresent",
		ServerThreads:                8,
		Port:                         9065,
		MetricsPort:                  9096,
		EnableAccessTimeEviction:     true,
		EvictionLastAccessThreshold:  "30d",
		EvictionSizeThreshold:        "80",
		EvictionPassFrequency:        "1h",
		EvictionMaintainFreeSpace:    true,
		EvictionSizeTarget:           "70",
		EvictionBucketInterval:       "1h",
		InfinibandResourceLimitCount: 0,
		InfinibandResourceLimitName:  "rdma/hca_rdma_infiniband",
		MountSsdPath:                 "/var/lib/ssd/cacheserver",
		Name:                         "mycache",
		ServerSizeInGB:               10,
		NodeCacheFolderPath:          "/var/lib/ssd/cacheserver",
		UseHostPath:                  true,
		SnapshotIntervalSeconds:      60,
	}
}

// TestRenderTemplate_NoYAMLInjection is the injection regression test. Even if
// validation were bypassed and a crafted selector value reached the renderer,
// JSON encoding must keep it a scalar so it cannot add PodSpec structure.
func TestRenderTemplate_NoYAMLInjection(t *testing.T) {
	payload := "gpu]\n      nodeName: victim-node\n" +
		"      automountServiceAccountToken: false\n" +
		"      initContainers:\n" +
		"      - name: injected\n" +
		"        image: busybox:1.36\n" +
		"        command: [\"sh\",\"-c\",\"true\"]\n" +
		"        volumeMounts:\n" +
		"        - name: cache-data-volume\n" +
		"          mountPath: /host\n" +
		"      tolerations:\n" +
		"      - operator: Exists\n" +
		"      sink: [x"

	v := baseRenderValues()
	v.NodeSelectorValue = []string{payload}

	var sts appsv1.StatefulSet
	if err := renderTemplateInto("templates/cacheserver-app.yaml", v, &sts); err != nil {
		t.Fatalf("render failed: %v", err)
	}

	pod := sts.Spec.Template.Spec
	if got := len(pod.Containers); got != 1 {
		t.Errorf("expected exactly 1 container, got %d", got)
	}
	if got := len(pod.InitContainers); got != 0 {
		t.Errorf("expected no init containers, got %d (injection succeeded)", got)
	}
	if pod.NodeName != "" {
		t.Errorf("expected empty nodeName, got %q (injection succeeded)", pod.NodeName)
	}
	if got := len(pod.Tolerations); got != 0 {
		t.Errorf("expected no tolerations, got %d (injection succeeded)", got)
	}

	// The payload must survive as a single, intact label value.
	terms := pod.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms
	if len(terms) != 1 || len(terms[0].MatchExpressions) != 1 {
		t.Fatalf("unexpected node affinity shape: %#v", terms)
	}
	vals := terms[0].MatchExpressions[0].Values
	if len(vals) != 1 || vals[0] != payload {
		t.Errorf("expected selector value preserved as one scalar, got %#v", vals)
	}
}

// TestRenderTemplate_NoInfinibandKeyInjection ensures the InfiniBand resource
// name cannot inject an extra resource map entry.
func TestRenderTemplate_NoInfinibandKeyInjection(t *testing.T) {
	v := baseRenderValues()
	v.InfinibandResourceLimitCount = 1
	v.InfinibandResourceLimitName = "rdma/x\n            evil.com/steal: \"1"

	var sts appsv1.StatefulSet
	if err := renderTemplateInto("templates/cacheserver-app.yaml", v, &sts); err != nil {
		// Rejecting the malformed document is an acceptable outcome.
		return
	}

	res := sts.Spec.Template.Spec.Containers[0].Resources
	for name := range res.Requests {
		if name == "evil.com/steal" {
			t.Errorf("injected resource key present in requests")
		}
	}
	for name := range res.Limits {
		if name == "evil.com/steal" {
			t.Errorf("injected resource key present in limits")
		}
	}
}

// TestRenderTemplate_ValidInputStillWorks guards against the encoding change
// breaking normal rendering.
func TestRenderTemplate_ValidInputStillWorks(t *testing.T) {
	v := baseRenderValues()
	var sts appsv1.StatefulSet
	if err := renderTemplateInto("templates/cacheserver-app.yaml", v, &sts); err != nil {
		t.Fatalf("render failed for valid input: %v", err)
	}
	terms := sts.Spec.Template.Spec.Affinity.NodeAffinity.RequiredDuringSchedulingIgnoredDuringExecution.NodeSelectorTerms
	if len(terms) != 1 {
		t.Fatalf("expected 1 node selector term, got %d", len(terms))
	}
	me := terms[0].MatchExpressions[0]
	if me.Key != "dacs.azure.com/processing-unit" {
		t.Errorf("unexpected selector key %q", me.Key)
	}
	if len(me.Values) != 1 || me.Values[0] != "gpu" {
		t.Errorf("unexpected selector values %#v", me.Values)
	}
	vols := sts.Spec.Template.Spec.Volumes
	found := false
	for _, vol := range vols {
		if vol.Name == "cache-data-volume" && vol.HostPath != nil {
			if vol.HostPath.Path != "/var/lib/ssd/cacheserver" {
				t.Errorf("unexpected hostPath %q", vol.HostPath.Path)
			}
			found = true
		}
	}
	if !found {
		t.Errorf("cache-data-volume hostPath not found")
	}
}

func TestRenderTemplate_CachePathIsEnvironmentData(t *testing.T) {
	v := baseRenderValues()
	v.MountSsdPath = `/var/lib/$(touch /tmp/inj); "quoted"`

	var sts appsv1.StatefulSet
	if err := renderTemplateInto("templates/cacheserver-app.yaml", v, &sts); err != nil {
		t.Fatalf("render failed: %v", err)
	}

	container := sts.Spec.Template.Spec.Containers[0]
	expectedPath := v.MountSsdPath + "/" + v.Name + "/cache"
	foundPath := false
	for _, env := range container.Env {
		if env.Name == "CACHESERVER_CACHE_PATH" {
			foundPath = true
			if env.Value != expectedPath {
				t.Errorf("expected cache path %q, got %q", expectedPath, env.Value)
			}
		}
	}
	if !foundPath {
		t.Fatal("CACHESERVER_CACHE_PATH environment variable not found")
	}

	script := strings.Join(container.Args, "\n")
	if strings.Contains(script, v.MountSsdPath) {
		t.Errorf("cache path was rendered into shell source: %q", script)
	}
	for _, command := range []string{
		`mkdir -p "$CACHESERVER_CACHE_PATH"`,
		`/root/check_space_overuse.sh "$CACHESERVER_CACHE_PATH" "$CACHESERVER_CACHE_SIZE_BYTES"`,
		`/root/CacheServer --port "$CACHESERVER_PORT" --cacheRoot "$CACHESERVER_CACHE_PATH" --sizeLimitBytes "$CACHESERVER_CACHE_SIZE_BYTES"`,
	} {
		if !strings.Contains(script, command) {
			t.Errorf("rendered command does not preserve argument boundaries: %q", command)
		}
	}
}

// TestValidateRenderInputs_FailClosed ensures the controller rejects Cache
// objects that reach rendering with required fields unset (e.g. when the
// admission webhook is disabled or was bypassed), instead of panicking on a
// nil pointer dereference during rendering.
func TestValidateRenderInputs_FailClosed(t *testing.T) {
	validName := "dacs.azure.com/processing-unit"
	validValue := "gpu"
	ibName := "rdma/hca_rdma_infiniband"
	var ibCount int32 = 1

	newValidCache := func() *storagev1.Cache {
		return &storagev1.Cache{
			Spec: storagev1.CacheSpec{
				NodeSelectorKey:              ptr.To(validName),
				NodeSelectorValue:            ptr.To(validValue),
				InfinibandResourceLimitName:  ptr.To(ibName),
				InfinibandResourceLimitCount: ptr.To(ibCount),
				Eviction: &storagev1.EvictionConfig{
					EnableAccessTimeEviction:  ptr.To(true),
					LastAccessThreshold:       ptr.To("30d"),
					SizeThreshold:             ptr.To("90"),
					PassFrequency:             ptr.To("15m"),
					EvictionMaintainFreeSpace: ptr.To(true),
					SizeTarget:                ptr.To("80"),
					BucketInterval:            ptr.To("15m"),
				},
			},
		}
	}

	// Sanity: a fully populated Cache passes.
	if err := validateRenderInputs(newValidCache(), storagev1.DefaultNodeCacheFolderPath); err != nil {
		t.Fatalf("expected valid cache to pass, got %v", err)
	}

	tests := []struct {
		name   string
		mutate func(c *storagev1.Cache)
	}{
		{"nil nodeSelectorKey", func(c *storagev1.Cache) { c.Spec.NodeSelectorKey = nil }},
		{"nil nodeSelectorValue", func(c *storagev1.Cache) { c.Spec.NodeSelectorValue = nil }},
		{"nil infinibandResourceLimitName", func(c *storagev1.Cache) { c.Spec.InfinibandResourceLimitName = nil }},
		{"nil infinibandResourceLimitCount", func(c *storagev1.Cache) { c.Spec.InfinibandResourceLimitCount = nil }},
		{"nil eviction", func(c *storagev1.Cache) { c.Spec.Eviction = nil }},
		{"nil enableAccessTimeEviction", func(c *storagev1.Cache) { c.Spec.Eviction.EnableAccessTimeEviction = nil }},
		{"nil evictionMaintainFreeSpace", func(c *storagev1.Cache) { c.Spec.Eviction.EvictionMaintainFreeSpace = nil }},
		{"nil lastAccessThreshold", func(c *storagev1.Cache) { c.Spec.Eviction.LastAccessThreshold = nil }},
		{"nil passFrequency", func(c *storagev1.Cache) { c.Spec.Eviction.PassFrequency = nil }},
		{"nil sizeThreshold", func(c *storagev1.Cache) { c.Spec.Eviction.SizeThreshold = nil }},
		{"nil sizeTarget", func(c *storagev1.Cache) { c.Spec.Eviction.SizeTarget = nil }},
		{"nil bucketInterval", func(c *storagev1.Cache) { c.Spec.Eviction.BucketInterval = nil }},
		{"injected bucketInterval", func(c *storagev1.Cache) {
			c.Spec.Eviction.BucketInterval = ptr.To("15m\"\n        - name: LD_PRELOAD\n          value: \"/evil.so")
		}},
		{"oversized bucketInterval", func(c *storagev1.Cache) { c.Spec.Eviction.BucketInterval = ptr.To("8d") }},
		{"zero bucketInterval", func(c *storagev1.Cache) { c.Spec.Eviction.BucketInterval = ptr.To("0s") }},
		{"injected lastAccessThreshold", func(c *storagev1.Cache) { c.Spec.Eviction.LastAccessThreshold = ptr.To("30d\"\n        - name: X") }},
		{"injected sizeTarget", func(c *storagev1.Cache) { c.Spec.Eviction.SizeTarget = ptr.To("80\"\n        - name: X") }},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			c := newValidCache()
			tc.mutate(c)
			if err := validateRenderInputs(c, storagev1.DefaultNodeCacheFolderPath); err == nil {
				t.Errorf("expected error for %s, got nil", tc.name)
			}
		})
	}
}

// TestRenderTemplate_NoEvictionEnvInjection ensures the eviction string fields,
// which are rendered as container environment variable values, cannot add
// PodSpec structure even if validation were bypassed.
func TestRenderTemplate_NoEvictionEnvInjection(t *testing.T) {
	payload := "15m\"\n        - name: LD_PRELOAD\n          value: \"/evil.so"

	v := baseRenderValues()
	v.EvictionBucketInterval = payload

	var sts appsv1.StatefulSet
	if err := renderTemplateInto("templates/cacheserver-app.yaml", v, &sts); err != nil {
		t.Fatalf("render failed: %v", err)
	}

	container := sts.Spec.Template.Spec.Containers[0]
	var found bool
	for _, env := range container.Env {
		if env.Name == "LD_PRELOAD" {
			t.Errorf("injected env var present (injection succeeded)")
		}
		if env.Name == "CACHESERVER_EVICTION_BUCKET_INTERVAL" {
			found = true
			if env.Value != payload {
				t.Errorf("expected payload preserved as one scalar, got %q", env.Value)
			}
		}
	}
	if !found {
		t.Errorf("CACHESERVER_EVICTION_BUCKET_INTERVAL not rendered")
	}
}

// locatorIndexEnv returns the rendered CACHESERVER_LOCATOR_INDEX value and
// whether the variable is present.
func locatorIndexEnv(t *testing.T, v renderValues) (string, bool) {
	t.Helper()
	var sts appsv1.StatefulSet
	if err := renderTemplateInto("templates/cacheserver-app.yaml", v, &sts); err != nil {
		t.Fatalf("render failed: %v", err)
	}
	for _, env := range sts.Spec.Template.Spec.Containers[0].Env {
		if env.Name == "CACHESERVER_LOCATOR_INDEX" {
			return env.Value, true
		}
	}
	return "", false
}

// TestRenderTemplate_LocatorIndexOffByDefault ensures that without the
// UseFlatRadixLocatorIndex gate the variable is not rendered at all, so the
// cache server keeps its default index and upgrading the controller does not
// change the StatefulSet of existing caches.
func TestRenderTemplate_LocatorIndexOffByDefault(t *testing.T) {
	if value, found := locatorIndexEnv(t, baseRenderValues()); found {
		t.Errorf("CACHESERVER_LOCATOR_INDEX rendered as %q with the gate off", value)
	}
}

// TestRenderTemplate_LocatorIndexFlatRadix ensures the gate selects the radix
// index on the cache server.
func TestRenderTemplate_LocatorIndexFlatRadix(t *testing.T) {
	v := baseRenderValues()
	v.UseFlatRadixLocatorIndex = true
	value, found := locatorIndexEnv(t, v)
	if !found {
		t.Fatal("CACHESERVER_LOCATOR_INDEX not rendered with the gate on")
	}
	if value != "flatradix" {
		t.Errorf("expected CACHESERVER_LOCATOR_INDEX=flatradix, got %q", value)
	}
}
