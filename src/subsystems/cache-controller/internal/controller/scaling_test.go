/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package controller

import (
	"context"
	"fmt"
	"math"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/types"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/utils/ptr"
	"sigs.k8s.io/controller-runtime/pkg/client"
)

// createDynamicCache creates a Cache CR configured for dynamic scaling.
func createDynamicCache(name, namespace string, percentage int32, minServers, maxServers *int32, selectorKey, selectorValue *string) *storagev1.Cache {
	return &storagev1.Cache{
		ObjectMeta: metav1.ObjectMeta{
			Name:      name,
			Namespace: namespace,
		},
		Spec: storagev1.CacheSpec{
			Scaling: storagev1.ScalingType{
				Dynamic: &storagev1.DynamicScaleConfig{
					PercentageOfNodesToUse: percentage,
					MinServers:             minServers,
					MaxServers:             maxServers,
				},
			},
			NodeSelectorKey:   selectorKey,
			NodeSelectorValue: selectorValue,
			ServerSizeInGB:    100,
			ServerThreads:     16,
			CpuLimit:          ptr.To[int32](1),
			MemoryLimitMB:     ptr.To[int32](20480),
			Storage: &storagev1.StorageConfig{
				Ephemeral: ptr.To(true),
			},
		},
	}
}

// createLabeledNode creates a Node with the given name and labels.
func createLabeledNode(name string, labels map[string]string) *corev1.Node {
	return &corev1.Node{
		ObjectMeta: metav1.ObjectMeta{
			Name:   name,
			Labels: labels,
		},
	}
}

func verifyConfigMapServerCount(ctx context.Context, namespace string, expectedCount int) error {
	var cm corev1.ConfigMap
	err := k8sClient.Get(ctx, types.NamespacedName{Name: "cacheserver-discovery-config", Namespace: namespace}, &cm)
	if err != nil {
		return fmt.Errorf("failed to get ConfigMap: %w", err)
	}

	actual, exists := cm.Data["cache_server_count"]
	if !exists {
		return fmt.Errorf("cache_server_count key not found in ConfigMap data")
	}
	expected := fmt.Sprintf("%d", expectedCount)
	if actual != expected {
		return fmt.Errorf("cache_server_count mismatch: expected %s, got %s", expected, actual)
	}
	return nil
}

var _ = Describe("Scaling Controller", func() {
	ctx := context.Background()
	SetDefaultEventuallyTimeout(5 * time.Second)
	SetDefaultEventuallyPollingInterval(1 * time.Millisecond)

	selectorKey := "test.azure.com/node-type"
	selectorValue := "cache-eligible"

	ensureNamespace := func(name string) {
		namespace := &corev1.Namespace{
			ObjectMeta: metav1.ObjectMeta{
				Name: name,
			},
		}
		err := k8sClient.Create(ctx, namespace)
		if err != nil && !errors.IsAlreadyExists(err) {
			Expect(err).NotTo(HaveOccurred(), "failed to create namespace %s: %v", name, err)
		}
	}

	// getScalingStatefulSet retrieves the StatefulSet owned by a dynamic Cache CR.
	getScalingStatefulSet := func(ns, cacheName string) *appsv1.StatefulSet {
		var stsList appsv1.StatefulSetList
		err := k8sClient.List(ctx, &stsList,
			client.InNamespace(ns),
			client.MatchingLabels{CacheNameLabelKey: cacheName},
		)
		if err != nil || len(stsList.Items) == 0 {
			return nil
		}
		return &stsList.Items[0]
	}

	getScalingReplicas := func(ns, cacheName string) int32 {
		sts := getScalingStatefulSet(ns, cacheName)
		if sts == nil || sts.Spec.Replicas == nil {
			return -1
		}
		return *sts.Spec.Replicas
	}

	expectReplicas := func(g Gomega, ns, cacheName string, expected int32) {
		g.Expect(getScalingReplicas(ns, cacheName)).To(Equal(expected),
			"expected %d replicas for Cache %s/%s", expected, ns, cacheName)
		g.Expect(verifyConfigMapServerCount(ctx, ns, int(expected))).To(Succeed(),
			"ConfigMap should reflect %d servers for Cache %s/%s", expected, ns, cacheName)
	}

	cleanupCache := func(ns, name string) {
		var cache storagev1.Cache
		if err := k8sClient.Get(ctx, types.NamespacedName{Name: name, Namespace: ns}, &cache); err == nil {
			Expect(k8sClient.Delete(ctx, &cache)).To(Succeed(), "failed to delete Cache %s/%s", ns, name)
		}
		Eventually(func(g Gomega) {
			var c storagev1.Cache
			err := k8sClient.Get(ctx, types.NamespacedName{Name: name, Namespace: ns}, &c)
			g.Expect(errors.IsNotFound(err)).To(BeTrue(), "expected Cache %s/%s to be deleted, but got error: %v", ns, name, err)
		}).Should(Succeed(), "Cache %s/%s should be deleted", ns, name)
	}

	cleanupNamespace := func(name string) {
		var ns corev1.Namespace
		if err := k8sClient.Get(ctx, types.NamespacedName{Name: name}, &ns); err == nil {
			Expect(k8sClient.Delete(ctx, &ns)).To(Succeed(), "failed to delete namespace %s", name)
		}
	}

	cleanupAllNodes := func() {
		var nodeList corev1.NodeList
		Expect(k8sClient.List(ctx, &nodeList)).To(Succeed(), "failed to list nodes for cleanup")
		for i := range nodeList.Items {
			Expect(k8sClient.Delete(ctx, &nodeList.Items[i])).To(Succeed(), "failed to delete node %s", nodeList.Items[i].Name)
		}
	}

	Context("When nodes match the cache's node selector", Ordered, func() {
		const ns = "scaling-selector"
		cacheName := "dynamic-cache"

		BeforeAll(func() {
			ensureNamespace(ns)
			DeferCleanup(cleanupNamespace, ns)
			DeferCleanup(cleanupCache, ns, cacheName)
			DeferCleanup(cleanupAllNodes)
			By("creating labeled nodes")
			for _, name := range []string{"gpu-node-0", "gpu-node-1", "gpu-node-2", "gpu-node-3"} {
				node := createLabeledNode(name, map[string]string{selectorKey: selectorValue})
				Expect(k8sClient.Create(ctx, node)).To(Succeed(), "failed to create labeled node %s", name)
			}
		})

		It("should scale the StatefulSet based on percentage of matching nodes", func() {
			By("creating a Cache CR with dynamic scaling at 50%")
			cache := createDynamicCache(cacheName, ns, 50, nil, nil, &selectorKey, &selectorValue)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed(), "failed to create Cache CR %s/%s", ns, cacheName)

			By("verifying the StatefulSet is created by the CRD controller")
			Eventually(func() *appsv1.StatefulSet {
				return getScalingStatefulSet(ns, cacheName)
			}).ShouldNot(BeNil(), "StatefulSet for Cache %s/%s was not created", ns, cacheName)

			By("verifying the ScalingReconciler scales to 50% of 4 nodes = 2 replicas")
			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 2)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 2 for 50%% of 4 nodes")
		})

		It("should scale up correctly as nodes are added one by one", func() {
			for i := 4; i <= 9; i++ {
				nodeName := fmt.Sprintf("gpu-node-%d", i)
				totalNodes := i + 1
				By(fmt.Sprintf("adding %s (total: %d nodes)", nodeName, totalNodes))
				node := createLabeledNode(nodeName, map[string]string{selectorKey: selectorValue})
				Expect(k8sClient.Create(ctx, node)).To(Succeed(), "failed to create node %s", nodeName)

				expectedReplicas := int32(math.Ceil(float64(totalNodes) * 0.5))
				Eventually(func(g Gomega) {
					expectReplicas(g, ns, cacheName, expectedReplicas)
				}).Should(Succeed(), "replicas and ConfigMap should converge after adding %s", nodeName)
			}
		})

		It("should scale down when a node is cordoned", func() {
			By("cordoning gpu-node-9")
			var node corev1.Node
			Expect(k8sClient.Get(ctx, types.NamespacedName{Name: "gpu-node-9"}, &node)).To(Succeed(), "failed to get node gpu-node-9")
			patch := client.MergeFrom(node.DeepCopy())
			node.Spec.Unschedulable = true
			Expect(k8sClient.Patch(ctx, &node, patch)).To(Succeed(), "failed to cordon node gpu-node-9")

			By("verifying the StatefulSet scales based on 9 schedulable nodes")
			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 5)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 5 after cordoning")
		})

		It("should scale back up when the node is uncordoned", func() {
			By("uncordoning gpu-node-9")
			var node corev1.Node
			Expect(k8sClient.Get(ctx, types.NamespacedName{Name: "gpu-node-9"}, &node)).To(Succeed(), "failed to get node gpu-node-9")
			patch := client.MergeFrom(node.DeepCopy())
			node.Spec.Unschedulable = false
			Expect(k8sClient.Patch(ctx, &node, patch)).To(Succeed(), "failed to uncordon node gpu-node-9")

			By("verifying the StatefulSet scales back up based on 10 schedulable nodes")
			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 5)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 5 after uncordoning")
		})

		It("should recreate the statefulset with proper replicas after deletion", func() {
			By("deleting the StatefulSet")
			sts := getScalingStatefulSet(ns, cacheName)
			Expect(sts).NotTo(BeNil(), "StatefulSet should exist before deletion")
			Expect(k8sClient.Delete(ctx, sts)).To(Succeed(), "failed to delete StatefulSet")

			By("verifying the StatefulSet is recreated by the CRD controller")
			Eventually(func() *appsv1.StatefulSet {
				return getScalingStatefulSet(ns, cacheName)
			}).ShouldNot(BeNil(), "StatefulSet for Cache %s/%s was not recreated after deletion", ns, cacheName)

			By("verifying the ScalingReconciler sets the correct replicas on the new StatefulSet")
			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 5)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 5 after StatefulSet recreation")
		})

		It("should increase replica count if percentage on cache cr is updated", func() {
			By("updating the Cache CR percentage to 80%")
			var cache storagev1.Cache
			Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: ns}, &cache)).To(Succeed(), "failed to get Cache CR for update")
			patch := client.MergeFrom(cache.DeepCopy())
			cache.Spec.Scaling.Dynamic.PercentageOfNodesToUse = 80
			Expect(k8sClient.Patch(ctx, &cache, patch)).To(Succeed(), "failed to update Cache CR percentage")

			By("verifying the StatefulSet scales up to 80% of 10 nodes = 8 replicas")
			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 8)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 8 for 80%% of 10 nodes")
		})

		It("should decrease replica count if percentage on cache cr is updated", func() {
			By("updating the Cache CR percentage to 30%")
			var cache storagev1.Cache
			Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: ns}, &cache)).To(Succeed(), "failed to get Cache CR for update")
			patch := client.MergeFrom(cache.DeepCopy())
			cache.Spec.Scaling.Dynamic.PercentageOfNodesToUse = 30
			Expect(k8sClient.Patch(ctx, &cache, patch)).To(Succeed(), "failed to update Cache CR percentage")

			By("verifying the StatefulSet scales down to 30% of 10 nodes = 3 replicas")
			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 3)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 3 for 30%% of 10 nodes")
		})
	})

	Context("When dynamic scaling has min/max bounds", Ordered, func() {
		const ns = "scaling-bounds"
		cacheName := "bounded-cache"

		BeforeAll(func() {
			ensureNamespace(ns)
			DeferCleanup(cleanupNamespace, ns)
			DeferCleanup(cleanupCache, ns, cacheName)
			DeferCleanup(cleanupAllNodes)
			By("creating 2 labeled nodes")
			for _, name := range []string{"bound-node-0", "bound-node-1"} {
				node := createLabeledNode(name, map[string]string{selectorKey: selectorValue})
				Expect(k8sClient.Create(ctx, node)).To(Succeed(), "failed to create labeled node %s", name)
			}
		})

		It("should respect minServers even when calculated replicas are lower", func() {
			By("creating a Cache CR with 50% of 2 nodes = 1, but minServers = 3")
			min := int32(3)
			cache := createDynamicCache(cacheName, ns, 50, &min, nil, &selectorKey, &selectorValue)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed(), "failed to create Cache CR %s/%s", ns, cacheName)

			By("verifying the StatefulSet is created")
			Eventually(func() *appsv1.StatefulSet {
				return getScalingStatefulSet(ns, cacheName)
			}).ShouldNot(BeNil(), "StatefulSet for Cache %s/%s was not created", ns, cacheName)

			By("verifying replicas are at least minServers = 3")
			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 3)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 3 (minServers override)")
		})
	})

	Context("When min/max bounds and percentage are adjusted dynamically", Ordered, func() {
		const ns = "scaling-adjust"
		cacheName := "adjustable-cache"

		BeforeAll(func() {
			ensureNamespace(ns)
			DeferCleanup(cleanupNamespace, ns)
			DeferCleanup(cleanupCache, ns, cacheName)
			DeferCleanup(cleanupAllNodes)
			By("creating 10 labeled nodes")
			for i := range 10 {
				node := createLabeledNode(fmt.Sprintf("adj-node-%d", i), map[string]string{selectorKey: selectorValue})
				Expect(k8sClient.Create(ctx, node)).To(Succeed(), "failed to create adj-node-%d", i)
			}
		})

		It("should start with 50% of 10 nodes = 5 replicas", func() {
			cache := createDynamicCache(cacheName, ns, 50, nil, nil, &selectorKey, &selectorValue)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed(), "failed to create Cache CR %s/%s", ns, cacheName)

			Eventually(func() *appsv1.StatefulSet {
				return getScalingStatefulSet(ns, cacheName)
			}).ShouldNot(BeNil(), "StatefulSet for %s/%s was not created", ns, cacheName)

			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 5)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 5 for 50%% of 10 nodes")
		})

		It("should clamp to maxServers when max is set below calculated replicas", func() {
			By("setting maxServers = 3 (calculated = 5, should clamp to 3)")
			Eventually(func(g Gomega) {
				var cache storagev1.Cache
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: ns}, &cache)).To(Succeed())
				patch := client.MergeFrom(cache.DeepCopy())
				cache.Spec.Scaling.Dynamic.MaxServers = ptr.To[int32](3)
				g.Expect(k8sClient.Patch(ctx, &cache, patch)).To(Succeed())
			}).Should(Succeed(), "failed to set maxServers")

			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 3)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 3 (maxServers clamp)")
		})

		It("should raise replicas to minServers when percentage yields fewer", func() {
			By("setting percentage to 10% (ceil(1) = 1) with minServers = 4")
			Eventually(func(g Gomega) {
				var cache storagev1.Cache
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: ns}, &cache)).To(Succeed())
				patch := client.MergeFrom(cache.DeepCopy())
				cache.Spec.Scaling.Dynamic.PercentageOfNodesToUse = 10
				cache.Spec.Scaling.Dynamic.MinServers = ptr.To[int32](4)
				cache.Spec.Scaling.Dynamic.MaxServers = nil
				g.Expect(k8sClient.Patch(ctx, &cache, patch)).To(Succeed())
			}).Should(Succeed(), "failed to set percentage and minServers")

			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 4)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 4 (minServers override)")
		})

		It("should clamp between min and max when both are set", func() {
			By("setting 100% with minServers = 2, maxServers = 7 (calculated = 10, clamped to 7)")
			Eventually(func(g Gomega) {
				var cache storagev1.Cache
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: ns}, &cache)).To(Succeed())
				patch := client.MergeFrom(cache.DeepCopy())
				cache.Spec.Scaling.Dynamic.PercentageOfNodesToUse = 100
				cache.Spec.Scaling.Dynamic.MinServers = ptr.To[int32](2)
				cache.Spec.Scaling.Dynamic.MaxServers = ptr.To[int32](7)
				g.Expect(k8sClient.Patch(ctx, &cache, patch)).To(Succeed())
			}).Should(Succeed(), "failed to set min/max bounds")

			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 7)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 7 (maxServers clamp)")
		})

		It("should respect minServers when percentage falls within min/max range", func() {
			By("setting 5% with minServers = 2, maxServers = 7 (calculated = ceil(0.5) = 1, clamped to min = 2)")
			Eventually(func(g Gomega) {
				var cache storagev1.Cache
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: ns}, &cache)).To(Succeed())
				patch := client.MergeFrom(cache.DeepCopy())
				cache.Spec.Scaling.Dynamic.PercentageOfNodesToUse = 5
				cache.Spec.Scaling.Dynamic.MinServers = ptr.To[int32](2)
				cache.Spec.Scaling.Dynamic.MaxServers = ptr.To[int32](7)
				g.Expect(k8sClient.Patch(ctx, &cache, patch)).To(Succeed())
			}).Should(Succeed(), "failed to set low percentage with min/max")

			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 2)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 2 (minServers clamp)")
		})

		It("should scale freely after removing min/max bounds", func() {
			By("setting 70% with no min/max (calculated = ceil(7) = 7)")
			Eventually(func(g Gomega) {
				var cache storagev1.Cache
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: ns}, &cache)).To(Succeed())
				patch := client.MergeFrom(cache.DeepCopy())
				cache.Spec.Scaling.Dynamic.PercentageOfNodesToUse = 70
				cache.Spec.Scaling.Dynamic.MinServers = nil
				cache.Spec.Scaling.Dynamic.MaxServers = nil
				g.Expect(k8sClient.Patch(ctx, &cache, patch)).To(Succeed())
			}).Should(Succeed(), "failed to remove min/max bounds")

			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 7)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 7 for 70%% of 10 nodes")
		})
	})

	Context("When multiple Cache CRs share the same node pool", Ordered, func() {
		const ns30 = "scaling-multi-30"
		const ns60 = "scaling-multi-60"
		cache30Name := "cache-30pct"
		cache60Name := "cache-60pct"

		BeforeAll(func() {
			ensureNamespace(ns30)
			ensureNamespace(ns60)
			DeferCleanup(cleanupNamespace, ns30)
			DeferCleanup(cleanupNamespace, ns60)
			DeferCleanup(cleanupCache, ns30, cache30Name)
			DeferCleanup(cleanupCache, ns60, cache60Name)
			DeferCleanup(cleanupAllNodes)

			By("creating 10 labeled nodes")
			for i := range 10 {
				node := createLabeledNode(fmt.Sprintf("multi-node-%d", i), map[string]string{selectorKey: selectorValue})
				Expect(k8sClient.Create(ctx, node)).To(Succeed(), "failed to create multi-node-%d", i)
			}
		})

		It("should scale each Cache CR independently based on its PercentageOfNodesToUse", func() {
			By("creating Cache CR with 30% in namespace A")
			cache30 := createDynamicCache(cache30Name, ns30, 30, nil, nil, &selectorKey, &selectorValue)
			Expect(k8sClient.Create(ctx, cache30)).To(Succeed())

			By("creating Cache CR with 60% in namespace B")
			cache60 := createDynamicCache(cache60Name, ns60, 60, nil, nil, &selectorKey, &selectorValue)
			Expect(k8sClient.Create(ctx, cache60)).To(Succeed())

			By("verifying Cache A scales to 3 replicas (30% of 10 nodes)")
			Eventually(func(g Gomega) {
				expectReplicas(g, ns30, cache30Name, 3)
			}).Should(Succeed(), "Cache A should converge to 3 replicas")

			By("verifying Cache B scales to 6 replicas (60% of 10 nodes)")
			Eventually(func(g Gomega) {
				expectReplicas(g, ns60, cache60Name, 6)
			}).Should(Succeed(), "Cache B should converge to 6 replicas")
		})
	})
})
