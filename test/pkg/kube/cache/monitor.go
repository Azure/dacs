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

// Package cache provides a controller-runtime informer that logs Cache CR
// activity during resilience tests.
package cache

import (
	"context"
	"fmt"
	"slices"

	"github.com/go-logr/logr"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	clientcache "k8s.io/client-go/tools/cache"
	"k8s.io/utils/ptr"
	"sigs.k8s.io/controller-runtime/pkg/cache"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
)

// AddMonitor logs every Cache CR change in the namespace: desired/current
// server counts and any False conditions (with reason and message).
func AddMonitor(ctx context.Context, runtimeCache cache.Cache, namespace string, log logr.Logger) error {
	informer, err := runtimeCache.GetInformer(ctx, &storagev1.Cache{})
	if err != nil {
		return fmt.Errorf("getting Cache informer: %w", err)
	}

	logCache := func(c *storagev1.Cache) {
		log.Info("cache",
			"name", c.Name,
			"desired", ptr.Deref(c.Status.DesiredServers, -1),
			"current", ptr.Deref(c.Status.CurrentServers, -1),
			"failing", failing(c.Status.Conditions),
		)
	}

	_, err = informer.AddEventHandler(clientcache.ResourceEventHandlerFuncs{
		AddFunc: func(obj interface{}) {
			c, ok := obj.(*storagev1.Cache)
			if !ok || c.Namespace != namespace {
				return
			}
			logCache(c)
		},
		UpdateFunc: func(oldObj, newObj interface{}) {
			newC, ok := newObj.(*storagev1.Cache)
			if !ok || newC.Namespace != namespace {
				return
			}
			if oldC, ok := oldObj.(*storagev1.Cache); ok && sameLoggedFields(oldC, newC) {
				return
			}
			logCache(newC)
		},
	})
	return err
}

// sameLoggedFields reports whether the fields logged by logCache are unchanged.
func sameLoggedFields(a, b *storagev1.Cache) bool {
	return ptr.Deref(a.Status.DesiredServers, -1) == ptr.Deref(b.Status.DesiredServers, -1) &&
		ptr.Deref(a.Status.CurrentServers, -1) == ptr.Deref(b.Status.CurrentServers, -1) &&
		slices.Equal(failing(a.Status.Conditions), failing(b.Status.Conditions))
}

// failing returns one entry per False condition: "Type=Reason: Message".
func failing(conds []metav1.Condition) []string {
	var out []string
	for _, c := range conds {
		if c.Status == metav1.ConditionFalse {
			out = append(out, fmt.Sprintf("%s=%s: %s", c.Type, c.Reason, c.Message))
		}
	}
	return out
}
