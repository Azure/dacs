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

// Package testutil provides shared test helpers for the cache controller.
// These are pure functions with no dependency on test infrastructure
// (k8sClient, ctx, etc.).
package testutil

import (
	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/utils/ptr"
)

// NewStaticCache returns a Cache CR configured for static scaling with the
// required spec fields pre-filled.
func NewStaticCache(name, namespace string, numServers int32) *storagev1.Cache {
	c := baseCache(name, namespace)
	c.Spec.Scaling.Static = &storagev1.StaticScaleConfig{NumServers: numServers}
	return c
}

// NewDynamicCache returns a Cache CR configured for dynamic scaling with the
// required spec fields pre-filled.
func NewDynamicCache(name, namespace string, percentage int32, selectorKey, selectorValue *string) *storagev1.Cache {
	c := baseCache(name, namespace)
	c.Spec.Scaling.Dynamic = &storagev1.DynamicScaleConfig{
		PercentageOfNodesToUse: percentage,
	}
	c.Spec.NodeSelectorKey = selectorKey
	c.Spec.NodeSelectorValue = selectorValue
	return c
}

// baseCache returns a Cache with all required fields populated.
func baseCache(name, namespace string) *storagev1.Cache {
	return &storagev1.Cache{
		ObjectMeta: metav1.ObjectMeta{
			Name:      name,
			Namespace: namespace,
		},
		Spec: storagev1.CacheSpec{
			ServerSizeInGB: 100,
			ServerThreads:  16,
			CpuLimit:       ptr.To[int32](1),
			MemoryLimitMB:  ptr.To[int32](20480),
			Storage:        &storagev1.StorageConfig{Ephemeral: ptr.To(true)},
		},
	}
}
