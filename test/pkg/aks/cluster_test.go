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

package aks

import (
	"os"
	"strings"
	"testing"

	"github.com/go-logr/logr"
)

func TestGetManagedClusterInfoExplicit(t *testing.T) {
	t.Setenv("AKS_CLUSTER_NAME", "aks-scale-123-1-1-cluster")
	t.Setenv("AKS_RESOURCE_GROUP", "dacs-integration")
	t.Setenv("PATH", t.TempDir())

	got, err := GetManagedClusterInfo(t.Context(), "subscription", "node-rg", logr.Discard())
	if err != nil {
		t.Fatalf("resolving explicit cluster without Azure CLI: %v", err)
	}
	want := ManagedClusterInfo{Name: "aks-scale-123-1-1-cluster", ResourceGroup: "dacs-integration"}
	if got != want {
		t.Fatalf("explicit cluster: got %+v, want %+v", got, want)
	}
}

func TestGetManagedClusterInfoIncomplete(t *testing.T) {
	for _, tc := range []struct {
		name, cluster, group string
	}{
		{name: "missing cluster", group: "dacs-integration"},
		{name: "missing group", cluster: "aks-scale-123-1-1-cluster"},
		{name: "empty values"},
		{name: "whitespace cluster", cluster: " ", group: "dacs-integration"},
		{name: "whitespace group", cluster: "aks-scale-123-1-1-cluster", group: " "},
	} {
		t.Run(tc.name, func(t *testing.T) {
			t.Setenv("AKS_CLUSTER_NAME", tc.cluster)
			t.Setenv("AKS_RESOURCE_GROUP", tc.group)
			t.Setenv("PATH", t.TempDir())
			_, err := GetManagedClusterInfo(t.Context(), "subscription", "node-rg", logr.Discard())
			if err == nil || err.Error() != "AKS_CLUSTER_NAME and AKS_RESOURCE_GROUP must both be nonempty when AKS_CLUSTER_NAME is set" {
				t.Fatalf("expected incomplete explicit metadata error, got %v", err)
			}

		})
	}
}

func TestGetManagedClusterInfoDiscoveryFallback(t *testing.T) {
	for _, group := range []string{"", "personal-cluster-rg"} {
		t.Run("group="+group, func(t *testing.T) {
			t.Setenv("AKS_CLUSTER_NAME", "")
			if err := os.Unsetenv("AKS_CLUSTER_NAME"); err != nil {
				t.Fatalf("clearing explicit cluster name for discovery test: %v", err)
			}
			t.Setenv("AKS_RESOURCE_GROUP", group)
			t.Setenv("PATH", t.TempDir())
			_, err := GetManagedClusterInfo(t.Context(), "subscription", "node-rg", logr.Discard())
			if err == nil || !strings.Contains(err.Error(), "failed to query tags for resource group node-rg") {
				t.Fatalf("expected original node-RG discovery without explicit cluster name, got %v", err)
			}
		})
	}
}
