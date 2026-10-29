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
	"encoding/json"
	"slices"
	"testing"
)

func mixedPools() []NodePool {
	return []NodePool{
		{Name: "nvmepool", Mode: "System", Count: 2, OSSKU: "AzureLinux3"},
		{Name: "az3fips", Mode: "User", Count: 1, OSSKU: "AzureLinux3", EnableFIPS: true},
		{Name: "ub2204", Mode: "User", Count: 1, OSSKU: "Ubuntu2204"},
		{Name: "ub22fips", Mode: "User", Count: 1, OSSKU: "Ubuntu2204", EnableFIPS: true},
		{Name: "ub2404", Mode: "User", Count: 1, OSSKU: "Ubuntu2404"},
		{Name: "acl", Mode: "User", Count: 1, OSSKU: "AzureContainerLinux"},
	}
}

func TestPlanNodePoolScale(t *testing.T) {
	for _, tt := range []struct {
		name  string
		pools []NodePool
		mixed bool
		want  NodePoolScalePlan
	}{
		{
			name:  "single system pool",
			pools: []NodePool{{Name: "nvmepool", Mode: "System", Count: 6}},
			want:  NodePoolScalePlan{"nvmepool", 6, 1, 6, 1},
		},
		{
			name:  "preserve other pools when system pool is not first",
			pools: []NodePool{{Name: "user", Mode: "User", Count: 2}, {Name: "system", Mode: "System", Count: 3}},
			want:  NodePoolScalePlan{"system", 3, 1, 5, 3},
		},
		{
			name:  "first pool fallback",
			pools: []NodePool{{Name: "user", Mode: "User", Count: 3}},
			want:  NodePoolScalePlan{"user", 3, 1, 3, 1},
		},
		{
			name:  "mixed ACL pool reaches zero without scaling system pools",
			pools: mixedPools(), mixed: true,
			want: NodePoolScalePlan{"acl", 1, 0, 7, 6},
		},
		{
			name: "ARM64 mixed ACL pool reaches zero",
			pools: slices.DeleteFunc(mixedPools(), func(p NodePool) bool {
				return p.Name == "ub22fips"
			}),
			mixed: true,
			want:  NodePoolScalePlan{"acl", 1, 0, 6, 5},
		},
	} {
		t.Run(tt.name, func(t *testing.T) {
			got, err := PlanNodePoolScale(tt.pools, tt.mixed)
			if err != nil {
				t.Fatalf("planning node pool scale: %v", err)
			}
			if got != tt.want {
				t.Fatalf("scale plan = %+v, want %+v", got, tt.want)
			}
		})
	}
}

func TestPlanNodePoolScaleRejectsInvalidInventory(t *testing.T) {
	for _, tt := range []struct {
		name  string
		pools []NodePool
		mixed bool
	}{
		{name: "empty"},
		{name: "already at target", pools: []NodePool{{Name: "system", Mode: "System", Count: 1}}},
		{name: "missing ACL", pools: mixedPools()[:5], mixed: true},
		{name: "ACL is a system pool", pools: []NodePool{{Name: "acl", Mode: "System", Count: 2}}, mixed: true},
		{name: "ACL already empty", pools: []NodePool{{Name: "acl", Mode: "User"}}, mixed: true},
		{name: "negative unselected count", pools: []NodePool{{Name: "system", Mode: "System", Count: 2}, {Name: "user", Count: -1}}},
	} {
		t.Run(tt.name, func(t *testing.T) {
			if _, err := PlanNodePoolScale(tt.pools, tt.mixed); err == nil {
				t.Fatal("expected invalid inventory to fail scale planning")
			}
		})
	}
}

func TestValidateMixedNodePools(t *testing.T) {
	if err := ValidateMixedNodePools(mixedPools(), "amd64"); err != nil {
		t.Fatalf("validating the complete mixed profile: %v", err)
	}
	for _, tt := range []struct {
		name   string
		mutate func([]NodePool) []NodePool
	}{
		{name: "missing profile", mutate: func(p []NodePool) []NodePool { return p[:5] }},
		{name: "FIPS disabled", mutate: func(p []NodePool) []NodePool { p[1].EnableFIPS = false; return p }},
		{name: "wrong OS", mutate: func(p []NodePool) []NodePool { p[5].OSSKU = "Ubuntu"; return p }},
		{name: "wrong count", mutate: func(p []NodePool) []NodePool { p[0].Count = 6; return p }},
		{name: "duplicate profile", mutate: func(p []NodePool) []NodePool { p[5] = p[4]; return p }},
	} {
		t.Run(tt.name, func(t *testing.T) {
			pools := tt.mutate(mixedPools())
			if err := ValidateMixedNodePools(pools, "amd64"); err == nil {
				t.Fatal("expected incorrect mixed profile to fail validation")
			}
		})
	}
	armPools := slices.DeleteFunc(mixedPools(), func(p NodePool) bool {
		return p.Name == "ub22fips"
	})
	if err := ValidateMixedNodePools(armPools, "arm64"); err != nil {
		t.Fatalf("validating ARM64 profiles without Ubuntu FIPS: %v", err)
	}
	if err := ValidateMixedNodePools(mixedPools(), "arm64"); err == nil {
		t.Fatal("expected Ubuntu FIPS on ARM64 to fail validation")
	}
	if err := ValidateMixedNodePools(armPools, "amd64"); err == nil {
		t.Fatal("expected missing Ubuntu FIPS on AMD64 to fail validation")
	}
	if err := ValidateMixedNodePools(mixedPools(), "invalid"); err == nil {
		t.Fatal("expected unsupported architecture to fail validation")
	}
	var pools []NodePool
	err := json.Unmarshal([]byte(`[{"name":"az3fips","mode":"User","count":1,"osSku":"AzureLinux3","enableFips":true}]`), &pools)
	if err != nil || len(pools) != 1 || !pools[0].EnableFIPS || pools[0].OSSKU != "AzureLinux3" {
		t.Fatalf("decoding Azure CLI pool image fields: pools=%+v, err=%v", pools, err)
	}
}

func TestSupportsMixedNodeOSVersion(t *testing.T) {
	for _, version := range []string{"1.34", "1.34.0", "v1.35.6", "1.36.99"} {
		if !SupportsMixedNodeOSVersion(version) {
			t.Errorf("expected Kubernetes %q to support the mixed profile", version)
		}
	}
	for _, version := range []string{"", "latest", "1.33.9", "1.37.0", "1.35.0-beta.1"} {
		if SupportsMixedNodeOSVersion(version) {
			t.Errorf("expected Kubernetes %q to be outside the mixed profile support window", version)
		}
	}
}
