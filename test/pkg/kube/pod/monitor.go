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

package pod

import (
	"context"
	"fmt"
	"sync"

	"github.com/go-logr/logr"
	corev1 "k8s.io/api/core/v1"
	clientcache "k8s.io/client-go/tools/cache"
	"sigs.k8s.io/controller-runtime/pkg/cache"
)

// RestartTracker reports container restarts that happened after AddMonitor was called.
type RestartTracker struct {
	mu       sync.Mutex
	baseline map[string]int32
	deltas   map[string]int32
}

// Snapshot returns a copy of the current restart deltas keyed by "podName/containerName".
// An empty map means no restarts were observed since AddMonitor was called.
func (r *RestartTracker) Snapshot() map[string]int32 {
	r.mu.Lock()
	defer r.mu.Unlock()
	out := make(map[string]int32, len(r.deltas))
	for k, v := range r.deltas {
		out[k] = v
	}
	return out
}

// AddMonitor registers an event handler on the controller-runtime cache to
// track container restart count changes in the specified namespace. Restarts
// that occurred before AddMonitor was called are excluded from the result via
// a baseline snapshot, so the returned tracker only reflects restarts caused
// by the test.
func AddMonitor(ctx context.Context, runtimeCache cache.Cache, namespace string, log logr.Logger) (*RestartTracker, error) {
	tracker := &RestartTracker{
		baseline: make(map[string]int32),
		deltas:   make(map[string]int32),
	}

	informer, err := runtimeCache.GetInformer(ctx, &corev1.Pod{})
	if err != nil {
		return nil, fmt.Errorf("getting pod informer: %w", err)
	}

	_, err = informer.AddEventHandler(clientcache.ResourceEventHandlerFuncs{
		AddFunc: func(obj interface{}) {
			p, ok := obj.(*corev1.Pod)
			if !ok || p.Namespace != namespace {
				return
			}
			tracker.mu.Lock()
			defer tracker.mu.Unlock()
			for _, cs := range p.Status.ContainerStatuses {
				key := fmt.Sprintf("%s/%s", p.Name, cs.Name)
				if _, seen := tracker.baseline[key]; !seen {
					tracker.baseline[key] = cs.RestartCount
				}
			}
		},
		UpdateFunc: func(_, newObj interface{}) {
			p, ok := newObj.(*corev1.Pod)
			if !ok || p.Namespace != namespace {
				return
			}
			tracker.mu.Lock()
			defer tracker.mu.Unlock()
			for _, cs := range p.Status.ContainerStatuses {
				key := fmt.Sprintf("%s/%s", p.Name, cs.Name)
				base, seen := tracker.baseline[key]
				if !seen {
					// New container observed after monitor started; treat
					// initial count as baseline (e.g., a pod created mid-test
					// with no restarts isn't counted).
					tracker.baseline[key] = cs.RestartCount
					continue
				}
				delta := cs.RestartCount - base
				if delta > 0 && tracker.deltas[key] != delta {
					log.Info("container restart detected",
						"container", key,
						"restartsThisTest", delta,
						"totalRestarts", cs.RestartCount)
					tracker.deltas[key] = delta
				}
			}
		},
	})

	return tracker, err
}
