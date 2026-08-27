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
	"strings"
	"testing"

	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/utils/ptr"
)

func TestCacheWebhook_Default(t *testing.T) {
	t.Parallel()

	cache := &Cache{Spec: CacheSpec{ServerSizeInGB: 100, ServerThreads: 8}}
	err := cache.Default(context.Background(), cache)
	if err != nil {
		t.Fatalf("Default() error: %v", err)
	}

	if *cache.Spec.Port != DefaultPort {
		t.Errorf("Port: got %v, want %v", *cache.Spec.Port, DefaultPort)
	}
	if *cache.Spec.MetricsPort != DefaultMetricsPort {
		t.Errorf("MetricsPort: got %v, want %v", *cache.Spec.MetricsPort, DefaultMetricsPort)
	}
	if *cache.Spec.NodeSelectorKey != DefaultNodeSelectorKey {
		t.Errorf("NodeSelectorKey: got %v, want %v", *cache.Spec.NodeSelectorKey, DefaultNodeSelectorKey)
	}
	if *cache.Spec.NodeSelectorValue != DefaultNodeSelectorValue {
		t.Errorf("NodeSelectorValue: got %v, want %v", *cache.Spec.NodeSelectorValue, DefaultNodeSelectorValue)
	}
	if *cache.Spec.InfinibandResourceLimitName != DefaultInfinibandResourceLimitName {
		t.Errorf("InfinibandResourceLimitName: got %v, want %v", *cache.Spec.InfinibandResourceLimitName, DefaultInfinibandResourceLimitName)
	}
	if *cache.Spec.InfinibandResourceLimitCount != DefaultInfinibandResourceLimitCount {
		t.Errorf("InfinibandResourceLimitCount: got %v, want %v", *cache.Spec.InfinibandResourceLimitCount, DefaultInfinibandResourceLimitCount)
	}

	if cache.Spec.Eviction == nil {
		t.Fatal("Eviction should not be nil")
	}
	if *cache.Spec.Eviction.EnableAccessTimeEviction != DefaultEnableAccessTimeEviction {
		t.Errorf("EnableAccessTimeEviction: got %v, want %v", *cache.Spec.Eviction.EnableAccessTimeEviction, DefaultEnableAccessTimeEviction)
	}
	if *cache.Spec.Eviction.EvictionMaintainFreeSpace != DefaultEvictionMaintainFreeSpace {
		t.Errorf("EvictionMaintainFreeSpace: got %v, want %v", *cache.Spec.Eviction.EvictionMaintainFreeSpace, DefaultEvictionMaintainFreeSpace)
	}
	if *cache.Spec.Eviction.LastAccessThreshold != DefaultLastAccessThreshold {
		t.Errorf("LastAccessThreshold: got %v, want %v", *cache.Spec.Eviction.LastAccessThreshold, DefaultLastAccessThreshold)
	}
	if *cache.Spec.Eviction.SizeThreshold != DefaultSizeThreshold {
		t.Errorf("SizeThreshold: got %v, want %v", *cache.Spec.Eviction.SizeThreshold, DefaultSizeThreshold)
	}
	if *cache.Spec.Eviction.PassFrequency != DefaultPassFrequency {
		t.Errorf("PassFrequency: got %v, want %v", *cache.Spec.Eviction.PassFrequency, DefaultPassFrequency)
	}
	if *cache.Spec.Eviction.SizeTarget != DefaultSizeTarget {
		t.Errorf("SizeTarget: got %v, want %v", *cache.Spec.Eviction.SizeTarget, DefaultSizeTarget)
	}

	if cache.Spec.Storage == nil || cache.Spec.Storage.HostPath == nil {
		t.Fatal("Storage.HostPath should not be nil")
	}
	if cache.Spec.Storage.HostPath.NodeCacheFolderPath != DefaultNodeCacheFolderPath {
		t.Errorf("NodeCacheFolderPath: got %v, want %v", cache.Spec.Storage.HostPath.NodeCacheFolderPath, DefaultNodeCacheFolderPath)
	}

	if cache.Spec.Scaling.Static == nil {
		t.Fatal("Scaling.Static should not be nil")
	}
	if cache.Spec.Scaling.Static.NumServers != DefaultNumServers {
		t.Errorf("NumServers: got %v, want %v", cache.Spec.Scaling.Static.NumServers, DefaultNumServers)
	}
}

func TestCacheWebhook_Default_DoesNotOverride(t *testing.T) {
	t.Parallel()

	cache := &Cache{
		Spec: CacheSpec{
			ServerSizeInGB:               100,
			ServerThreads:                8,
			Port:                         ptr.To(int32(8080)),
			MetricsPort:                  ptr.To(int32(8081)),
			NodeSelectorKey:              ptr.To("custom-key"),
			NodeSelectorValue:            ptr.To("custom-value"),
			InfinibandResourceLimitName:  ptr.To("custom/infiniband"),
			InfinibandResourceLimitCount: ptr.To(int32(2)),
			Eviction: &EvictionConfig{
				EnableAccessTimeEviction:  ptr.To(false),
				EvictionMaintainFreeSpace: ptr.To(false),
				LastAccessThreshold:       ptr.To("7d"),
				SizeThreshold:             ptr.To("80"),
				PassFrequency:             ptr.To("2h"),
				SizeTarget:                ptr.To("70"),
			},
			Storage: &StorageConfig{
				HostPath: &HostPathConfig{NodeCacheFolderPath: "/custom/path"},
			},
		},
	}

	err := cache.Default(context.Background(), cache)
	if err != nil {
		t.Fatalf("Default() error: %v", err)
	}

	if *cache.Spec.Port != 8080 {
		t.Errorf("Port was overwritten: got %v, want 8080", *cache.Spec.Port)
	}
	if *cache.Spec.MetricsPort != 8081 {
		t.Errorf("MetricsPort was overwritten: got %v, want 8081", *cache.Spec.MetricsPort)
	}
	if *cache.Spec.NodeSelectorKey != "custom-key" {
		t.Errorf("NodeSelectorKey was overwritten: got %v, want custom-key", *cache.Spec.NodeSelectorKey)
	}
	if *cache.Spec.NodeSelectorValue != "custom-value" {
		t.Errorf("NodeSelectorValue was overwritten: got %v, want custom-value", *cache.Spec.NodeSelectorValue)
	}
	if *cache.Spec.InfinibandResourceLimitName != "custom/infiniband" {
		t.Errorf("InfinibandResourceLimitName was overwritten: got %v, want custom/infiniband", *cache.Spec.InfinibandResourceLimitName)
	}
	if *cache.Spec.InfinibandResourceLimitCount != 2 {
		t.Errorf("InfinibandResourceLimitCount was overwritten: got %v, want 2", *cache.Spec.InfinibandResourceLimitCount)
	}
	if *cache.Spec.Eviction.EnableAccessTimeEviction != false {
		t.Errorf("EnableAccessTimeEviction was overwritten: got %v, want false", *cache.Spec.Eviction.EnableAccessTimeEviction)
	}
	if *cache.Spec.Eviction.EvictionMaintainFreeSpace != false {
		t.Errorf("EvictionMaintainFreeSpace was overwritten: got %v, want false", *cache.Spec.Eviction.EvictionMaintainFreeSpace)
	}
	if *cache.Spec.Eviction.LastAccessThreshold != "7d" {
		t.Errorf("LastAccessThreshold was overwritten: got %v, want 7d", *cache.Spec.Eviction.LastAccessThreshold)
	}
	if *cache.Spec.Eviction.SizeThreshold != "80" {
		t.Errorf("SizeThreshold was overwritten: got %v, want 80", *cache.Spec.Eviction.SizeThreshold)
	}
	if *cache.Spec.Eviction.PassFrequency != "2h" {
		t.Errorf("PassFrequency was overwritten: got %v, want 2h", *cache.Spec.Eviction.PassFrequency)
	}
	if *cache.Spec.Eviction.SizeTarget != "70" {
		t.Errorf("SizeTarget was overwritten: got %v, want 70", *cache.Spec.Eviction.SizeTarget)
	}
	if cache.Spec.Storage.HostPath.NodeCacheFolderPath != "/custom/path" {
		t.Errorf("NodeCacheFolderPath was overwritten: got %v, want /custom/path", cache.Spec.Storage.HostPath.NodeCacheFolderPath)
	}
}

func TestCacheWebhook_ValidateCreate(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name        string
		mutate      func(*Cache)
		expectedErr string
	}{
		{
			name: "valid default",
		},
		{
			name: "rejects empty name and empty generateName",
			mutate: func(c *Cache) {
				c.Name = ""
			},
			expectedErr: "must specify metadata.name or metadata.generateName",
		},
		{
			name: "accepts generateName instead of name",
			mutate: func(c *Cache) {
				c.Name = ""
				c.GenerateName = "my-cache-"
			},
		},
		{
			name: "accepts generateName at max length (48 chars)",
			mutate: func(c *Cache) {
				c.Name = ""
				c.GenerateName = strings.Repeat("a", MaxCacheGenerateNameLength)
			},
		},
		{
			name: "rejects generateName exceeding 48 characters",
			mutate: func(c *Cache) {
				c.Name = ""
				c.GenerateName = strings.Repeat("a", MaxCacheGenerateNameLength+1)
			},
			expectedErr: "must be at most 48 characters",
		},
		{
			name: "rejects generateName with uppercase letters",
			mutate: func(c *Cache) {
				c.Name = ""
				c.GenerateName = "MyCache-"
			},
			expectedErr: "a lowercase RFC 1123 label must consist of",
		},
		{
			name: "rejects generateName starting with hyphen",
			mutate: func(c *Cache) {
				c.Name = ""
				c.GenerateName = "-my-cache-"
			},
			expectedErr: "a lowercase RFC 1123 label must consist of",
		},
		{
			name: "accepts generateName ending with hyphen",
			mutate: func(c *Cache) {
				c.Name = ""
				c.GenerateName = "my-cache-"
			},
		},
		{
			name: "accepts name at max length (53 chars)",
			mutate: func(c *Cache) {
				c.Name = strings.Repeat("a", MaxCacheNameLength)
			},
		},
		{
			name: "rejects name exceeding 53 characters",
			mutate: func(c *Cache) {
				c.Name = strings.Repeat("a", MaxCacheNameLength+1)
			},
			expectedErr: "must be at most 53 characters",
		},
		{
			name: "rejects name with uppercase letters",
			mutate: func(c *Cache) {
				c.Name = "MyCache"
			},
			expectedErr: "a lowercase RFC 1123 label must consist of",
		},
		{
			name: "rejects name starting with hyphen",
			mutate: func(c *Cache) {
				c.Name = "-mycache"
			},
			expectedErr: "a lowercase RFC 1123 label must consist of",
		},
		{
			name: "rejects name ending with hyphen",
			mutate: func(c *Cache) {
				c.Name = "mycache-"
			},
			expectedErr: "a lowercase RFC 1123 label must consist of",
		},
		{
			name: "rejects name with dots",
			mutate: func(c *Cache) {
				c.Name = "my.cache"
			},
			expectedErr: "must not contain dots",
		},
		{
			name: "rejects name ending in -discovery (cacheserver-discovery)",
			mutate: func(c *Cache) {
				c.Name = "cacheserver-discovery"
			},
			expectedErr: `must not end with "-discovery"`,
		},
		{
			name: "valid with dynamic scaling",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
				}
			},
		},
		{
			name: "valid with dynamic scaling and minServers",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
					MinServers:             ptr.To(int32(3)),
				}
			},
		},
		{
			name: "valid with dynamic scaling and maxServers",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
					MaxServers:             ptr.To(int32(10)),
				}
			},
		},
		{
			name: "valid with dynamic scaling and both min and max",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
					MinServers:             ptr.To(int32(3)),
					MaxServers:             ptr.To(int32(10)),
				}
			},
		},
		{
			name: "valid with dynamic scaling and min equals max",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
					MinServers:             ptr.To(int32(5)),
					MaxServers:             ptr.To(int32(5)),
				}
			},
		},
		{
			name: "rejects minServers below 1",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
					MinServers:             ptr.To(int32(0)),
				}
			},
			expectedErr: "must be between 1 and 499",
		},
		{
			name: "rejects maxServers below 1",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
					MaxServers:             ptr.To(int32(0)),
				}
			},
			expectedErr: "must be between 1 and 499",
		},
		{
			name: "rejects minServers above 499",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
					MinServers:             ptr.To(int32(500)),
				}
			},
			expectedErr: "must be between 1 and 499",
		},
		{
			name: "rejects maxServers above 499",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
					MaxServers:             ptr.To(int32(500)),
				}
			},
			expectedErr: "must be between 1 and 499",
		},
		{
			name: "rejects negative minServers",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
					MinServers:             ptr.To(int32(-1)),
				}
			},
			expectedErr: "must be between 1 and 499",
		},
		{
			name: "rejects negative maxServers",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
					MaxServers:             ptr.To(int32(-1)),
				}
			},
			expectedErr: "must be between 1 and 499",
		},
		{
			name: "rejects minServers exceeding maxServers",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
					MinServers:             ptr.To(int32(10)),
					MaxServers:             ptr.To(int32(5)),
				}
			},
			expectedErr: "cannot exceed maxServers",
		},
		{
			name: "rejects ephemeral and hostPath together",
			mutate: func(c *Cache) {
				c.Spec.Storage = &StorageConfig{Ephemeral: ptr.To(true), HostPath: &HostPathConfig{NodeCacheFolderPath: "/data"}}
			},
			expectedErr: "cannot specify both ephemeral and hostPath",
		},
		{
			name: "rejects invalid duration format",
			mutate: func(c *Cache) {
				c.Spec.Eviction = &EvictionConfig{LastAccessThreshold: ptr.To("invalid")}
			},
			expectedErr: "invalid duration format",
		},
		{
			name: "rejects invalid percentage",
			mutate: func(c *Cache) {
				c.Spec.Eviction = &EvictionConfig{SizeThreshold: ptr.To("150")}
			},
			expectedErr: "must be between 0 and 100",
		},
		{
			name: "rejects same port and metricsPort",
			mutate: func(c *Cache) {
				c.Spec.Port = ptr.To(int32(9065))
				c.Spec.MetricsPort = ptr.To(int32(9065))
			},
			expectedErr: "must be different from port",
		},
		{
			name: "rejects sizeTarget >= sizeThreshold",
			mutate: func(c *Cache) {
				c.Spec.Eviction = &EvictionConfig{
					SizeThreshold: ptr.To("80"),
					SizeTarget:    ptr.To("90"),
				}
			},
			expectedErr: "sizeTarget (90%) must be less than sizeThreshold (80%)",
		},
		{
			name: "rejects infinibandResourceLimitCount > 0 without name",
			mutate: func(c *Cache) {
				c.Spec.InfinibandResourceLimitName = ptr.To("")
				c.Spec.InfinibandResourceLimitCount = ptr.To(int32(1))
			},
			expectedErr: "required when infinibandResourceLimitCount > 0",
		},
		// Boundary tests for resource limits
		{
			name: "rejects serverSizeInGB = 0",
			mutate: func(c *Cache) {
				c.Spec.ServerSizeInGB = 0
			},
			expectedErr: "must be at least 1",
		},
		{
			name: "accepts serverSizeInGB = 1",
			mutate: func(c *Cache) {
				c.Spec.ServerSizeInGB = 1
			},
		},
		{
			name: "rejects serverThreads = 3",
			mutate: func(c *Cache) {
				c.Spec.ServerThreads = 3
			},
			expectedErr: "must be at least 4",
		},
		{
			name: "accepts serverThreads = 4",
			mutate: func(c *Cache) {
				c.Spec.ServerThreads = 4
			},
		},
		{
			name: "rejects cpuLimit = -1",
			mutate: func(c *Cache) {
				c.Spec.CpuLimit = ptr.To(int32(-1))
			},
			expectedErr: "must be at least 0",
		},
		{
			name: "accepts cpuLimit = 0",
			mutate: func(c *Cache) {
				c.Spec.CpuLimit = ptr.To(int32(0))
			},
		},
		{
			name: "accepts cpuLimit = 1",
			mutate: func(c *Cache) {
				c.Spec.CpuLimit = ptr.To(int32(1))
			},
		},
		{
			name: "rejects memoryLimitMB = 1023",
			mutate: func(c *Cache) {
				c.Spec.MemoryLimitMB = ptr.To(int32(1023))
			},
			expectedErr: "must be 0 or at least 1024",
		},
		{
			name: "accepts memoryLimitMB = 0",
			mutate: func(c *Cache) {
				c.Spec.MemoryLimitMB = ptr.To(int32(0))
			},
		},
		{
			name: "accepts memoryLimitMB = 1024",
			mutate: func(c *Cache) {
				c.Spec.MemoryLimitMB = ptr.To(int32(1024))
			},
		},
		{
			name: "rejects egressBandwidthLimitGbps = 0",
			mutate: func(c *Cache) {
				c.Spec.EgressBandwidthLimitGbps = ptr.To(int32(0))
			},
			expectedErr: "must be at least 1",
		},
		{
			name: "accepts egressBandwidthLimitGbps = 1",
			mutate: func(c *Cache) {
				c.Spec.EgressBandwidthLimitGbps = ptr.To(int32(1))
			},
		},
		{
			name: "rejects percentageOfNodesToUse = 0",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 0,
				}
			},
			expectedErr: "must be between 1 and 100",
		},
		{
			name: "rejects percentageOfNodesToUse = 101",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Dynamic = &DynamicScaleConfig{
					PercentageOfNodesToUse: 101,
				}
			},
			expectedErr: "must be between 1 and 100",
		},
		{
			name: "rejects static numServers = 0",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Static = &StaticScaleConfig{NumServers: 0}
			},
			expectedErr: "must be at least 1",
		},
		{
			name: "rejects static numServers = 500",
			mutate: func(c *Cache) {
				c.Spec.Scaling.Static = &StaticScaleConfig{NumServers: 500}
			},
			expectedErr: "must be at most 499",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			cache := &Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "test-cache"},
				Spec:       CacheSpec{ServerSizeInGB: 100, ServerThreads: 8},
			}
			if tt.mutate != nil {
				tt.mutate(cache)
			}

			if err := cache.Default(context.Background(), cache); err != nil {
				t.Fatalf("Default() error: %v", err)
			}

			_, err := cache.ValidateCreate(context.Background(), cache)

			if tt.expectedErr == "" {
				if err != nil {
					t.Errorf("expected no error, got: %v", err)
				}
			} else {
				if err == nil {
					t.Errorf("expected error containing %q, got nil", tt.expectedErr)
				} else if !strings.Contains(err.Error(), tt.expectedErr) {
					t.Errorf("expected error containing %q, got: %v", tt.expectedErr, err)
				}
			}
		})
	}
}

func TestCacheWebhook_ValidateUpdate(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name        string
		mutateOld   func(*Cache)
		mutateNew   func(*Cache)
		expectedErr string
	}{
		{
			name: "valid update with no changes",
		},
		{
			name: "valid update changing serverSizeInGB",
			mutateNew: func(c *Cache) {
				c.Spec.ServerSizeInGB = 200
			},
		},
		{
			name: "rejects invalid serverThreads on update",
			mutateNew: func(c *Cache) {
				c.Spec.ServerThreads = 2
			},
			expectedErr: "must be at least 4",
		},
		{
			name: "rejects invalid memoryLimitMB on update",
			mutateNew: func(c *Cache) {
				c.Spec.MemoryLimitMB = ptr.To(int32(512))
			},
			expectedErr: "must be 0 or at least 1024",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()
			oldCache := &Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "test-cache"},
				Spec:       CacheSpec{ServerSizeInGB: 100, ServerThreads: 8},
			}
			if tt.mutateOld != nil {
				tt.mutateOld(oldCache)
			}
			if err := oldCache.Default(context.Background(), oldCache); err != nil {
				t.Fatalf("Default() old error: %v", err)
			}

			newCache := &Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "test-cache"},
				Spec:       CacheSpec{ServerSizeInGB: 100, ServerThreads: 8},
			}
			if tt.mutateNew != nil {
				tt.mutateNew(newCache)
			}
			if err := newCache.Default(context.Background(), newCache); err != nil {
				t.Fatalf("Default() new error: %v", err)
			}

			_, err := newCache.ValidateUpdate(context.Background(), oldCache, newCache)

			if tt.expectedErr == "" {
				if err != nil {
					t.Errorf("expected no error, got: %v", err)
				}
			} else {
				if err == nil {
					t.Errorf("expected error containing %q, got nil", tt.expectedErr)
				} else if !strings.Contains(err.Error(), tt.expectedErr) {
					t.Errorf("expected error containing %q, got: %v", tt.expectedErr, err)
				}
			}
		})
	}
}

func TestCacheWebhook_ValidateDelete(t *testing.T) {
	t.Parallel()

	cache := &Cache{
		ObjectMeta: metav1.ObjectMeta{Name: "test-cache"},
		Spec:       CacheSpec{ServerSizeInGB: 100, ServerThreads: 8},
	}
	if err := cache.Default(context.Background(), cache); err != nil {
		t.Fatalf("Default() error: %v", err)
	}

	_, err := cache.ValidateDelete(context.Background(), cache)
	if err != nil {
		t.Errorf("ValidateDelete should accept deletion, got: %v", err)
	}
}
