/*
Copyright (c) Microsoft Corporation.
Licensed under the Apache License, Version 2.0.
*/

package scaling

import (
	"context"
	"fmt"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/cache-controller/internal/config"
	"github.com/Azure/dacs/cache-controller/internal/statushelper"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes/scheme"
	"k8s.io/utils/ptr"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/client/interceptor"
	logf "sigs.k8s.io/controller-runtime/pkg/log"
)

var (
	log = logf.Log.WithName("scaling-reconciler-test")
)

func ensureNamespace(name string) {
	ns := &corev1.Namespace{ObjectMeta: metav1.ObjectMeta{Name: name}}
	err := k8sClient.Create(ctx, ns)
	if err != nil && !errors.IsAlreadyExists(err) {
		Expect(err).NotTo(HaveOccurred())
	}
}

func cleanupAllNodes() {
	var nodeList corev1.NodeList
	Expect(k8sClient.List(ctx, &nodeList)).To(Succeed())
	for i := range nodeList.Items {
		Expect(k8sClient.Delete(ctx, &nodeList.Items[i])).To(Succeed())
	}
}

var _ = Describe("Scaling Reconciler", func() {
	selectorKey := "test.azure.com/node-type"
	selectorValue := "scaling-test"

	// newReconciler builds a Reconciler backed by k8sClient with field indexes
	// registered on it. Since envtest's plain client doesn't support field indexes
	// out of the box, we use a client that wraps the REST config with a cache that
	// has indexes. For simplicity we create the reconciler with k8sClient directly
	// and rely on the fact that our tests use label selectors (which work without
	// indexes) where possible, or we test the functions that don't need indexes.

	Context("DesiredReplicas with static scaling", func() {
		It("should return the static replica count and set Scaling=nil", func() {
			cache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "static-test", Namespace: "default"},
				Spec: storagev1.CacheSpec{
					Scaling: storagev1.ScalingType{
						Static: &storagev1.StaticScaleConfig{NumServers: 7},
					},
				},
			}
			r := &Reconciler{Client: k8sClient, scheme: scheme.Scheme, normalizer: newNormalizer(&config.Config{})}
			h, err := statushelper.New(cache, k8sClient)
			Expect(err).NotTo(HaveOccurred())

			got, err := r.DesiredReplicas(ctx, cache, 0, h)
			Expect(err).NotTo(HaveOccurred())
			Expect(got).To(Equal(int32(7)))

			Expect(cache.Status.Scaling).To(BeNil(), "Scaling should be nil for static scaling")
		})

		It("should purge normalizer state so dynamic->static->dynamic starts fresh", func() {
			r := New(k8sClient, scheme.Scheme, &config.Config{})

			staticCache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "purge-switch", Namespace: "default"},
				Spec: storagev1.CacheSpec{
					Scaling: storagev1.ScalingType{
						Static: &storagev1.StaticScaleConfig{NumServers: 3},
					},
				},
			}
			key := client.ObjectKeyFromObject(staticCache)

			// Seed the normalizer with a high recommendation as if we were
			// running dynamic scaling with a stabilization window.
			dynamicConfig := &storagev1.DynamicScaleConfig{
				PercentageOfNodesToUse: 50,
			}
			_, err := r.normalizer.NormalizeDesiredReplicas(key, 10, 10, dynamicConfig, log)
			Expect(err).NotTo(HaveOccurred())

			// Switch to static -- this should purge the normalizer
			h, err := statushelper.New(staticCache, k8sClient)
			Expect(err).NotTo(HaveOccurred())
			got, err := r.DesiredReplicas(ctx, staticCache, 10, h)
			Expect(err).NotTo(HaveOccurred())
			Expect(got).To(Equal(int32(3)))

			// Now verify normalizer state was purged: calling NormalizeDesiredReplicas
			// with desired=5 and currentReplicas=5 should return 5 (no stale 10 in window).
			norm, err := r.normalizer.NormalizeDesiredReplicas(key, 5, 5, dynamicConfig, log)
			Expect(err).NotTo(HaveOccurred())
			Expect(norm.Replicas).To(Equal(int32(5)), "stale recommendation of 10 should be purged")
			Expect(norm.RequeueAfter).To(BeZero())
		})
	})

	Context("DesiredReplicas with no scaling config", func() {
		It("should return an error", func() {
			cache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "no-config", Namespace: "default"},
				Spec:       storagev1.CacheSpec{},
			}
			r := &Reconciler{Client: k8sClient, scheme: scheme.Scheme, normalizer: newNormalizer(&config.Config{})}
			h, err := statushelper.New(cache, k8sClient)
			Expect(err).NotTo(HaveOccurred())

			_, err = r.DesiredReplicas(ctx, cache, 0, h)
			Expect(err).To(HaveOccurred())
			Expect(err.Error()).To(ContainSubstring("does not have scaling configuration"))
		})
	})

	Context("DesiredReplicas with dynamic scaling", Ordered, func() {
		const ns = "scaling-unit"

		BeforeAll(func() {
			ensureNamespace(ns)
			DeferCleanup(cleanupAllNodes)

			By("creating 10 labeled nodes")
			for i := range 10 {
				node := &corev1.Node{
					ObjectMeta: metav1.ObjectMeta{
						Name:   fmt.Sprintf("scale-unit-node-%d", i),
						Labels: map[string]string{selectorKey: selectorValue},
					},
				}
				Expect(k8sClient.Create(ctx, node)).To(Succeed())
			}
		})

		It("should compute desired replicas on first reconciliation (currentReplicas=0)", func() {
			cache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "dyn-first", Namespace: ns},
				Spec: storagev1.CacheSpec{
					Scaling: storagev1.ScalingType{
						Dynamic: &storagev1.DynamicScaleConfig{
							PercentageOfNodesToUse: 50,
						},
					},
					NodeSelectorKey:   ptr.To(selectorKey),
					NodeSelectorValue: ptr.To(selectorValue),
				},
			}

			r := New(k8sClient, scheme.Scheme, &config.Config{})
			h, err := statushelper.New(cache, k8sClient)
			Expect(err).NotTo(HaveOccurred())

			got, err := r.DesiredReplicas(ctx, cache, 0, h)
			Expect(err).NotTo(HaveOccurred())
			Expect(got).To(Equal(int32(5)), "50%% of 10 nodes = 5")

			Expect(cache.Status.Scaling).NotTo(BeNil())
			Expect(cache.Status.Scaling.StabilizationEnds).To(BeNil(), "no stabilization window when settled")
			Expect(cache.Status.Scaling.TargetServers).To(Equal(ptr.To(int32(5))))
		})

		It("should return same count when no change needed (currentReplicas matches desired)", func() {
			cache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "dyn-noop", Namespace: ns},
				Spec: storagev1.CacheSpec{
					Scaling: storagev1.ScalingType{
						Dynamic: &storagev1.DynamicScaleConfig{
							PercentageOfNodesToUse: 50,
						},
					},
					NodeSelectorKey:   ptr.To(selectorKey),
					NodeSelectorValue: ptr.To(selectorValue),
				},
			}

			r := New(k8sClient, scheme.Scheme, &config.Config{})
			h, err := statushelper.New(cache, k8sClient)
			Expect(err).NotTo(HaveOccurred())

			got, err := r.DesiredReplicas(ctx, cache, 5, h)
			Expect(err).NotTo(HaveOccurred())
			Expect(got).To(Equal(int32(5)))

			Expect(cache.Status.Scaling).NotTo(BeNil())
			Expect(cache.Status.Scaling.StabilizationEnds).To(BeNil(), "no stabilization window when settled")
			Expect(cache.Status.Scaling.TargetServers).To(Equal(ptr.To(int32(5))))
		})

		It("should hold back scale-down when stabilization window is active", func() {
			cache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "dyn-stab", Namespace: ns},
				Spec: storagev1.CacheSpec{
					Scaling: storagev1.ScalingType{
						Dynamic: &storagev1.DynamicScaleConfig{
							PercentageOfNodesToUse: 50,
						},
					},
					NodeSelectorKey:   ptr.To(selectorKey),
					NodeSelectorValue: ptr.To(selectorValue),
				},
			}

			r := New(k8sClient, scheme.Scheme, &config.Config{
				ScaleDownStabilizationWindow: 5 * time.Minute,
			})

			// Pre-seed the normalizer by recording a recommendation of 8 (as if the
			// previous reconciliation computed 8 replicas). This establishes a
			// higher recommendation in the stabilization window.
			key := client.ObjectKeyFromObject(cache)
			_, err := r.normalizer.NormalizeDesiredReplicas(key, 8, 8, cache.Spec.Scaling.Dynamic, log)
			Expect(err).NotTo(HaveOccurred())

			// Now reconcile with 50% of 10 nodes -> desired=5, but stabilization
			// should hold at 8 because 8 is the highest recommendation in the window.
			h, err := statushelper.New(cache, k8sClient)
			Expect(err).NotTo(HaveOccurred())
			got, err := r.DesiredReplicas(ctx, cache, 8, h)
			Expect(err).NotTo(HaveOccurred())
			Expect(got).To(Equal(int32(8)), "stabilization should hold at current")
			Expect(h.RequeueAfter()).To(BeNumerically(">", 0), "should set RequeueAfter")

			Expect(cache.Status.Scaling).NotTo(BeNil())
			Expect(cache.Status.Scaling.StabilizationEnds).NotTo(BeNil())
			Expect(cache.Status.Scaling.TargetServers).To(Equal(ptr.To(int32(5))),
				"TargetServers should show what we'd scale to without stabilization")
		})

		It("should return error and set condition when calculator fails", func() {
			cache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "dyn-calcerr", Namespace: ns},
				Spec: storagev1.CacheSpec{
					Scaling: storagev1.ScalingType{
						Dynamic: &storagev1.DynamicScaleConfig{
							PercentageOfNodesToUse: 50,
						},
					},
					NodeSelectorKey:   ptr.To(selectorKey),
					NodeSelectorValue: ptr.To(selectorValue),
				},
			}

			r := New(k8sClient, scheme.Scheme, &config.Config{})
			r.calculator = func(int, int32, *int32, *int32) (int32, error) {
				return 0, fmt.Errorf("calculation failed")
			}
			h, err := statushelper.New(cache, k8sClient)
			Expect(err).NotTo(HaveOccurred())

			_, err = r.DesiredReplicas(ctx, cache, 0, h)
			Expect(err).To(HaveOccurred())
			Expect(err.Error()).To(ContainSubstring("calculation failed"))

			Expect(cache.Status.Scaling).NotTo(BeNil(), "Scaling should surface error message")
			Expect(cache.Status.Scaling.Message).To(ContainSubstring("Failed to calculate target servers"))
		})

		It("should return error and set condition when node list fails", func() {
			cache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "dyn-nodeselfail", Namespace: ns},
				Spec: storagev1.CacheSpec{
					Scaling: storagev1.ScalingType{
						Dynamic: &storagev1.DynamicScaleConfig{
							PercentageOfNodesToUse: 50,
						},
					},
					// Use a selector that doesn't match any field index, triggering a list error
					// by pointing to a non-existent field index value.
					NodeSelectorKey:   ptr.To("nonexistent.azure.com/label"),
					NodeSelectorValue: ptr.To("no-match"),
				},
			}

			// Create a reconciler whose client will return an error on List.
			// We achieve this by using a cancelled context.
			cancelledCtx, cancelFn := context.WithCancel(ctx)
			cancelFn() // immediately cancel

			r := New(k8sClient, scheme.Scheme, &config.Config{})
			h, err := statushelper.New(cache, k8sClient)
			Expect(err).NotTo(HaveOccurred())

			_, err = r.DesiredReplicas(cancelledCtx, cache, 0, h)
			Expect(err).To(HaveOccurred())
			Expect(err.Error()).To(ContainSubstring("failed to list nodes"))

			Expect(cache.Status.Scaling).NotTo(BeNil(), "Scaling should surface error message")
			Expect(cache.Status.Scaling.Message).To(ContainSubstring("Failed to list nodes"))
		})

		It("should handle no scaling configuration", func() {
			cache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "dyn-noscaling", Namespace: ns},
				Spec:       storagev1.CacheSpec{},
			}

			r := New(k8sClient, scheme.Scheme, &config.Config{})
			h, err := statushelper.New(cache, k8sClient)
			Expect(err).NotTo(HaveOccurred())

			_, err = r.DesiredReplicas(ctx, cache, 0, h)
			Expect(err).To(HaveOccurred())
			Expect(err.Error()).To(ContainSubstring("does not have scaling configuration"))

			Expect(cache.Status.Scaling).NotTo(BeNil(), "Scaling should surface error message")
			Expect(cache.Status.Scaling.Message).To(ContainSubstring("No scaling configuration found"))
		})

		It("should filter nodes by selector", func() {
			By("creating 3 nodes with a different label")
			otherKey := "test.azure.com/other"
			otherVal := "other-pool"
			for i := range 3 {
				node := &corev1.Node{
					ObjectMeta: metav1.ObjectMeta{
						Name:   fmt.Sprintf("other-node-%d", i),
						Labels: map[string]string{otherKey: otherVal},
					},
				}
				Expect(k8sClient.Create(ctx, node)).To(Succeed())
			}

			cache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "dyn-selector", Namespace: ns},
				Spec: storagev1.CacheSpec{
					Scaling: storagev1.ScalingType{
						Dynamic: &storagev1.DynamicScaleConfig{
							PercentageOfNodesToUse: 100,
						},
					},
					NodeSelectorKey:   ptr.To(otherKey),
					NodeSelectorValue: ptr.To(otherVal),
				},
			}

			r := New(k8sClient, scheme.Scheme, &config.Config{})
			h, err := statushelper.New(cache, k8sClient)
			Expect(err).NotTo(HaveOccurred())

			got, err := r.DesiredReplicas(ctx, cache, 0, h)
			Expect(err).NotTo(HaveOccurred())
			Expect(got).To(Equal(int32(3)), "should only count nodes matching the selector")
		})
	})

	Context("MapNodeToCaches", Ordered, func() {
		const ns = "map-node-test"

		BeforeAll(func() {
			ensureNamespace(ns)
		})

		It("should return nil for non-node objects", func() {
			r := &Reconciler{Client: k8sClient, scheme: scheme.Scheme}
			pod := &corev1.Pod{ObjectMeta: metav1.ObjectMeta{Name: "p1", Namespace: ns}}
			reqs := r.MapNodeToCaches(ctx, pod)
			Expect(reqs).To(BeNil())
		})

		It("should return nil for nodes with no labels", func() {
			r := &Reconciler{Client: k8sClient, scheme: scheme.Scheme}
			node := &corev1.Node{ObjectMeta: metav1.ObjectMeta{Name: "unlabeled"}}
			reqs := r.MapNodeToCaches(ctx, node)
			Expect(reqs).To(BeNil())
		})

		It("should return matching Cache CRs when node labels match selector", func() {
			By("creating Cache CRs with different selectors")
			matchKey := "pool"
			matchVal := "gpu"
			otherVal := "cpu"

			cacheMatch := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "cache-gpu", Namespace: ns},
				Spec: storagev1.CacheSpec{
					Scaling:           storagev1.ScalingType{Static: &storagev1.StaticScaleConfig{NumServers: 1}},
					NodeSelectorKey:   ptr.To(matchKey),
					NodeSelectorValue: ptr.To(matchVal),
					ServerSizeInGB:    100, ServerThreads: 16,
					CpuLimit: ptr.To[int32](1), MemoryLimitMB: ptr.To[int32](20480),
					Storage: &storagev1.StorageConfig{Ephemeral: ptr.To(true)},
				},
			}
			cacheNoMatch := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "cache-cpu", Namespace: ns},
				Spec: storagev1.CacheSpec{
					Scaling:           storagev1.ScalingType{Static: &storagev1.StaticScaleConfig{NumServers: 1}},
					NodeSelectorKey:   ptr.To(matchKey),
					NodeSelectorValue: ptr.To(otherVal),
					ServerSizeInGB:    100, ServerThreads: 16,
					CpuLimit: ptr.To[int32](1), MemoryLimitMB: ptr.To[int32](20480),
					Storage: &storagev1.StorageConfig{Ephemeral: ptr.To(true)},
				},
			}
			cacheNoSelector := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{Name: "cache-noselector", Namespace: ns},
				Spec: storagev1.CacheSpec{
					Scaling:        storagev1.ScalingType{Static: &storagev1.StaticScaleConfig{NumServers: 1}},
					ServerSizeInGB: 100, ServerThreads: 16,
					CpuLimit: ptr.To[int32](1), MemoryLimitMB: ptr.To[int32](20480),
					Storage: &storagev1.StorageConfig{Ephemeral: ptr.To(true)},
				},
			}
			Expect(k8sClient.Create(ctx, cacheMatch)).To(Succeed())
			Expect(k8sClient.Create(ctx, cacheNoMatch)).To(Succeed())
			Expect(k8sClient.Create(ctx, cacheNoSelector)).To(Succeed())
			DeferCleanup(func() {
				_ = k8sClient.Delete(ctx, cacheMatch)
				_ = k8sClient.Delete(ctx, cacheNoMatch)
				_ = k8sClient.Delete(ctx, cacheNoSelector)
			})

			r := &Reconciler{Client: k8sClient, scheme: scheme.Scheme}
			node := &corev1.Node{ObjectMeta: metav1.ObjectMeta{
				Name:   "gpu-node-1",
				Labels: map[string]string{matchKey: matchVal},
			}}

			reqs := r.MapNodeToCaches(ctx, node)
			Expect(reqs).To(HaveLen(1), "should match only the gpu cache")
			Expect(reqs[0].Name).To(Equal("cache-gpu"))
			Expect(reqs[0].Namespace).To(Equal(ns))
		})
	})

	Context("API server disruption", Ordered, func() {
		const ns = "disruption-test"

		var (
			failNodeList  bool
			failCacheList bool
			watchClient   client.WithWatch
		)

		// wrappedClient wraps the envtest watchClient with an interceptor
		// that can inject ServiceUnavailable errors on List calls.
		wrappedClient := func() client.Client {
			return interceptor.NewClient(watchClient, interceptor.Funcs{
				List: func(ctx context.Context, inner client.WithWatch, list client.ObjectList, opts ...client.ListOption) error {
					switch list.(type) {
					case *corev1.NodeList:
						if failNodeList {
							return errors.NewServiceUnavailable("apiserver not available")
						}
					case *storagev1.CacheList:
						if failCacheList {
							return errors.NewServiceUnavailable("apiserver not available")
						}
					}
					return inner.List(ctx, list, opts...)
				},
			})
		}

		BeforeAll(func() {
			var err error
			watchClient, err = client.NewWithWatch(cfg, client.Options{Scheme: scheme.Scheme})
			Expect(err).NotTo(HaveOccurred())

			ensureNamespace(ns)

			By("creating 6 labeled nodes for disruption tests")
			for i := range 6 {
				node := &corev1.Node{
					ObjectMeta: metav1.ObjectMeta{
						Name:   fmt.Sprintf("disruption-node-%d", i),
						Labels: map[string]string{selectorKey: selectorValue},
					},
				}
				Expect(k8sClient.Create(ctx, node)).To(Succeed())
			}
		})

		AfterAll(func() {
			var nodeList corev1.NodeList
			Expect(k8sClient.List(ctx, &nodeList, client.MatchingLabels{selectorKey: selectorValue})).To(Succeed())
			for i := range nodeList.Items {
				_ = k8sClient.Delete(ctx, &nodeList.Items[i])
			}
		})

		BeforeEach(func() {
			failNodeList = false
			failCacheList = false
		})

		It("should recover after transient node-list failure", func() {
			cache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{
					Name:      "disruption-recover",
					Namespace: ns,
				},
				Spec: storagev1.CacheSpec{
					Scaling: storagev1.ScalingType{
						Dynamic: &storagev1.DynamicScaleConfig{
							PercentageOfNodesToUse: 50,
						},
					},
					NodeSelectorKey:   ptr.To(selectorKey),
					NodeSelectorValue: ptr.To(selectorValue),
				},
			}

			r := New(wrappedClient(), scheme.Scheme, &config.Config{})

			By("injecting node-list failure")
			failNodeList = true
			h, err := statushelper.New(cache, k8sClient)
			Expect(err).NotTo(HaveOccurred())

			_, err = r.DesiredReplicas(ctx, cache, 0, h)
			Expect(err).To(HaveOccurred())
			Expect(errors.IsServiceUnavailable(err)).To(BeTrue())

			Expect(cache.Status.Scaling).NotTo(BeNil(), "Scaling should surface error message")
			Expect(cache.Status.Scaling.Message).To(ContainSubstring("Failed to list nodes"))

			By("removing failure and verifying recovery")
			failNodeList = false
			h, err = statushelper.New(cache, k8sClient)
			Expect(err).NotTo(HaveOccurred())

			got, err := r.DesiredReplicas(ctx, cache, 0, h)
			Expect(err).NotTo(HaveOccurred())
			Expect(got).To(Equal(int32(3)), "50%% of 6 nodes = 3")

			Expect(cache.Status.Scaling).NotTo(BeNil())
			Expect(cache.Status.Scaling.StabilizationEnds).To(BeNil(), "no stabilization window after seed")
			Expect(cache.Status.Scaling.TargetServers).To(Equal(ptr.To(int32(3))))
		})

		It("should preserve normalizer state across repeated failures", func() {
			cache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{
					Name:      "disruption-preserve",
					Namespace: ns,
				},
				Spec: storagev1.CacheSpec{
					Scaling: storagev1.ScalingType{
						Dynamic: &storagev1.DynamicScaleConfig{
							PercentageOfNodesToUse: 50,
						},
					},
					NodeSelectorKey:   ptr.To(selectorKey),
					NodeSelectorValue: ptr.To(selectorValue),
				},
			}

			r := New(wrappedClient(), scheme.Scheme, &config.Config{
				ScaleDownStabilizationWindow: 5 * time.Minute,
			})

			By("seeding normalizer with high recommendation")
			key := client.ObjectKeyFromObject(cache)
			_, err := r.normalizer.NormalizeDesiredReplicas(key, 6, 6, cache.Spec.Scaling.Dynamic, log)
			Expect(err).NotTo(HaveOccurred())

			By("injecting repeated failures")
			failNodeList = true
			for range 3 {
				h, err := statushelper.New(cache, k8sClient)
				Expect(err).NotTo(HaveOccurred())
				_, err = r.DesiredReplicas(ctx, cache, 6, h)
				Expect(err).To(HaveOccurred())
			}

			By("recovering and verifying stabilization still holds")
			failNodeList = false
			h, err := statushelper.New(cache, k8sClient)
			Expect(err).NotTo(HaveOccurred())

			got, err := r.DesiredReplicas(ctx, cache, 6, h)
			Expect(err).NotTo(HaveOccurred())
			Expect(got).To(Equal(int32(6)), "stabilization should hold seeded value")
			Expect(h.RequeueAfter()).To(BeNumerically(">", 0))

			Expect(cache.Status.Scaling).NotTo(BeNil())
			Expect(cache.Status.Scaling.StabilizationEnds).NotTo(BeNil())
			Expect(cache.Status.Scaling.TargetServers).To(Equal(ptr.To(int32(3))))
		})

		It("should return nil from MapNodeToCaches when cache-list fails", func() {
			r := &Reconciler{Client: wrappedClient(), scheme: scheme.Scheme}

			failCacheList = true
			node := &corev1.Node{
				ObjectMeta: metav1.ObjectMeta{
					Name:   "disruption-map-node",
					Labels: map[string]string{"pool": "default"},
				},
			}
			reqs := r.MapNodeToCaches(ctx, node)
			Expect(reqs).To(BeNil())
		})
	})

	Context("NewReconciler", func() {
		It("should create a reconciler with all fields populated", func() {
			r := New(k8sClient, scheme.Scheme, &config.Config{})
			Expect(r.Client).NotTo(BeNil())
			Expect(r.scheme).NotTo(BeNil())
			Expect(r.calculator).NotTo(BeNil())
			Expect(r.normalizer).NotTo(BeNil())
		})
	})

	Context("RealClock", func() {
		It("should return a non-zero time", func() {
			c := realClock{}
			Expect(c.Now().IsZero()).To(BeFalse())
		})
	})
})
