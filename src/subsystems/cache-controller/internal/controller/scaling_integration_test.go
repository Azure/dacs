/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package controller

import (
	"context"
	"fmt"
	"math"
	"strings"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/types"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/cache-controller/internal/testutil"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	eventsv1 "k8s.io/api/events/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/utils/ptr"
	"sigs.k8s.io/controller-runtime/pkg/client"
)

// createDynamicCache creates a Cache CR configured for dynamic scaling.
func createDynamicCache(name, namespace string, percentage int32, minServers, maxServers *int32, selectorKey, selectorValue *string) *storagev1.Cache {
	cache := testutil.NewDynamicCache(name, namespace, percentage, selectorKey, selectorValue)
	cache.Spec.Scaling.Dynamic.MinServers = minServers
	cache.Spec.Scaling.Dynamic.MaxServers = maxServers
	return cache
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

func verifyConfigMapServerCount(ctx context.Context, namespace, cacheName string, expectedCount int) error {
	var cm corev1.ConfigMap
	err := k8sClient.Get(ctx, types.NamespacedName{Name: cacheName + storagev1.DiscoveryConfigSuffix, Namespace: namespace}, &cm)
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
		g.Expect(verifyConfigMapServerCount(ctx, ns, cacheName, int(expected))).To(Succeed(),
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
			cache := testutil.NewDynamicCache(cacheName, ns, 50, &selectorKey, &selectorValue)
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

				expectedReplicas := int32(math.Floor(float64(totalNodes) * 0.5))
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
				expectReplicas(g, ns, cacheName, 4)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 4 after cordoning")
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

			By("verifying replicas are 0 (desired 1 < minServers 3)")
			Eventually(func(g Gomega) {
				expectReplicas(g, ns, cacheName, 0)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 0 (desired < minServers)")
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

		It("should floor to 0 when percentage yields fewer than minServers", func() {
			By("setting percentage to 10% (floor = 1) with minServers = 4")
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
				expectReplicas(g, ns, cacheName, 0)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 0 (desired < minServers)")
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

		It("should floor to 0 when percentage falls below minServers with both bounds set", func() {
			By("setting 5% with minServers = 2, maxServers = 7 (calculated = floor(0.5) = 0)")
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
				expectReplicas(g, ns, cacheName, 0)
			}).Should(Succeed(), "replicas and ConfigMap should converge to 0 (desired < minServers)")
		})

		It("should scale freely after removing min/max bounds", func() {
			By("setting 70% with no min/max (calculated = 7)")
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
			cache30 := testutil.NewDynamicCache(cache30Name, ns30, 30, &selectorKey, &selectorValue)
			Expect(k8sClient.Create(ctx, cache30)).To(Succeed())

			By("creating Cache CR with 60% in namespace B")
			cache60 := testutil.NewDynamicCache(cache60Name, ns60, 60, &selectorKey, &selectorValue)
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

	// getCondition reads the named condition from the Cache status.
	getCondition := func(ns, name, condType string) *metav1.Condition {
		var cache storagev1.Cache
		if err := k8sClient.Get(ctx, types.NamespacedName{Name: name, Namespace: ns}, &cache); err != nil {
			return nil
		}
		for i := range cache.Status.Conditions {
			if cache.Status.Conditions[i].Type == condType {
				return &cache.Status.Conditions[i]
			}
		}
		return nil
	}

	// getScalingStatus reads the ScalingStatus from the Cache status.
	getScalingStatus := func(ns, name string) *storagev1.ScalingStatus {
		var cache storagev1.Cache
		if err := k8sClient.Get(ctx, types.NamespacedName{Name: name, Namespace: ns}, &cache); err != nil {
			return nil
		}
		return cache.Status.Scaling
	}

	Context("When tolerance band suppresses scaling", Ordered, func() {
		const ns = "scaling-tolerance"
		cacheName := "tolerance-cache"

		BeforeAll(func() {
			ensureNamespace(ns)
			DeferCleanup(cleanupNamespace, ns)
			DeferCleanup(cleanupCache, ns, cacheName)
			DeferCleanup(cleanupAllNodes)

			By("creating 20 labeled nodes")
			for i := range 20 {
				node := createLabeledNode(fmt.Sprintf("tol-node-%d", i), map[string]string{selectorKey: selectorValue})
				Expect(k8sClient.Create(ctx, node)).To(Succeed(), "failed to create tol-node-%d", i)
			}
		})

		It("should initially scale to 50% of 20 nodes = 10 replicas", func() {
			By("creating a Cache CR with 10% tolerance (default)")
			cache := testutil.NewDynamicCache(cacheName, ns, 50, &selectorKey, &selectorValue)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed())

			Eventually(func() *appsv1.StatefulSet {
				return getScalingStatefulSet(ns, cacheName)
			}).ShouldNot(BeNil())

			Eventually(func(g Gomega) {
				g.Expect(getScalingReplicas(ns, cacheName)).To(Equal(int32(10)),
					"expected 10 replicas (50%% of 20 nodes)")
			}).Should(Succeed())
		})

		It("should NOT scale when adding 1 node (change within 10% tolerance)", func() {
			By("adding 2 node (22 nodes -> 50%% = floor(11) = 11, ratio 11/10 = 1.1 <= 1+0.1 -> suppressed)")
			node1 := createLabeledNode("tol-node-20", map[string]string{selectorKey: selectorValue})
			node2 := createLabeledNode("tol-node-21", map[string]string{selectorKey: selectorValue})
			Expect(k8sClient.Create(ctx, node1)).To(Succeed())
			Expect(k8sClient.Create(ctx, node2)).To(Succeed())

			By("waiting briefly to allow reconciliation")
			Consistently(func() int32 {
				return getScalingReplicas(ns, cacheName)
			}, "2s", "100ms").Should(Equal(int32(10)),
				"replicas should remain at 10 because the 1-node change is within 10%% tolerance")
		})

		It("should scale when the change exceeds tolerance", func() {
			By("adding 2 more nodes (24 nodes -> 50%% = floor(12.0) = 12, ratio 12/10 = 1.2 > 1.1 -> scales)")
			for _, name := range []string{"tol-node-22", "tol-node-23"} {
				node := createLabeledNode(name, map[string]string{selectorKey: selectorValue})
				Expect(k8sClient.Create(ctx, node)).To(Succeed())
			}

			Eventually(func(g Gomega) {
				g.Expect(getScalingReplicas(ns, cacheName)).To(Equal(int32(12)),
					"expected 12 replicas (50%% of 24 nodes = floor(12.0) = 12, exceeds tolerance)")
			}).Should(Succeed())
		})

		It("should report scaling settled (no stabilization window) when stable", func() {
			Eventually(func(g Gomega) {
				ss := getScalingStatus(ns, cacheName)
				g.Expect(ss).NotTo(BeNil(), "Scaling status should exist")
				g.Expect(ss.StabilizationEnds).To(BeNil(),
					"StabilizationEnds should be nil when no stabilization window is active")
			}).Should(Succeed())
		})
	})

	Context("When downscale stabilization window holds back scaling", Ordered, func() {
		const ns = "scaling-stabilize"
		cacheName := "stabilize-cache"

		BeforeAll(func() {
			ensureNamespace(ns)
			DeferCleanup(cleanupNamespace, ns)
			DeferCleanup(cleanupCache, ns, cacheName)
			DeferCleanup(cleanupAllNodes)

			By("creating 10 labeled nodes")
			for i := range 10 {
				node := createLabeledNode(fmt.Sprintf("stab-node-%d", i), map[string]string{selectorKey: selectorValue})
				Expect(k8sClient.Create(ctx, node)).To(Succeed())
			}
		})

		It("should initially scale to 50% of 10 nodes = 5 replicas", func() {
			By("creating a Cache CR with dynamic scaling")
			cache := testutil.NewDynamicCache(cacheName, ns, 50, &selectorKey, &selectorValue)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed())

			Eventually(func() *appsv1.StatefulSet {
				return getScalingStatefulSet(ns, cacheName)
			}).ShouldNot(BeNil())

			Eventually(func(g Gomega) {
				g.Expect(getScalingReplicas(ns, cacheName)).To(Equal(int32(5)),
					"expected 5 replicas (50%% of 10 nodes)")
			}).Should(Succeed())

			By("waiting for the normalizer to be seeded (settled after steady-state reconciliation)")
			Eventually(func(g Gomega) {
				ss := getScalingStatus(ns, cacheName)
				g.Expect(ss).NotTo(BeNil())
				g.Expect(ss.StabilizationEnds).To(BeNil())
			}).Should(Succeed(), "normalizer should have run with currentReplicas=5")
		})

		It("should hold back downscale during stabilization window", func() {
			By("deleting 6 nodes so 50% of 4 = 2 replicas desired")
			for i := 4; i < 10; i++ {
				node := &corev1.Node{}
				node.Name = fmt.Sprintf("stab-node-%d", i)
				Expect(k8sClient.Delete(ctx, node)).To(Succeed())
			}

			By("verifying replicas stay at 5 during the stabilization window")
			Consistently(func() int32 {
				return getScalingReplicas(ns, cacheName)
			}, "2s", "200ms").Should(Equal(int32(5)),
				"replicas should remain at 5 during the 3s downscale stabilization window")
		})

		It("should report stabilization window active while holding back scaling", func() {
			ss := getScalingStatus(ns, cacheName)
			Expect(ss).NotTo(BeNil(), "Scaling status should exist")
			Expect(ss.StabilizationEnds).NotTo(BeNil(),
				"StabilizationEnds should be set while stabilization window is active")
		})

		It("should scale down after the stabilization window expires", func() {
			By("waiting for the 3s window to expire and controller to requeue")
			Eventually(func(g Gomega) {
				g.Expect(getScalingReplicas(ns, cacheName)).To(Equal(int32(2)),
					"expected 2 replicas (50%% of 4 nodes) after stabilization window expires")
			}, "10s", "200ms").Should(Succeed())
		})

		It("should report scaling settled after window expires", func() {
			Eventually(func(g Gomega) {
				ss := getScalingStatus(ns, cacheName)
				g.Expect(ss).NotTo(BeNil())
				g.Expect(ss.StabilizationEnds).To(BeNil(),
					"StabilizationEnds should be nil once stabilization window has expired")
			}).Should(Succeed())
		})

		It("should scale up immediately (no stabilization for scale-up)", func() {
			By("increasing percentage to 100% (4 nodes = 4 replicas desired)")
			Eventually(func(g Gomega) {
				var cache storagev1.Cache
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: ns}, &cache)).To(Succeed())
				patch := client.MergeFrom(cache.DeepCopy())
				cache.Spec.Scaling.Dynamic.PercentageOfNodesToUse = 100
				g.Expect(k8sClient.Patch(ctx, &cache, patch)).To(Succeed())
			}).Should(Succeed())

			By("verifying immediate scale-up (spec change bypasses stabilization)")
			Eventually(func(g Gomega) {
				g.Expect(getScalingReplicas(ns, cacheName)).To(Equal(int32(4)),
					"expected 4 replicas (100%% of 4 nodes) -- spec change should be immediate")
			}).Should(Succeed())
		})
	})

	Context("When static scaling is configured", Ordered, func() {
		const ns = "scaling-static"
		cacheName := "static-cache"

		BeforeAll(func() {
			ensureNamespace(ns)
			DeferCleanup(cleanupNamespace, ns)
			DeferCleanup(cleanupCache, ns, cacheName)
		})

		It("should scale to the specified static replica count", func() {
			cache := testutil.NewStaticCache(cacheName, ns, 3)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed())

			Eventually(func() *appsv1.StatefulSet {
				return getScalingStatefulSet(ns, cacheName)
			}).ShouldNot(BeNil())

			Eventually(func(g Gomega) {
				g.Expect(getScalingReplicas(ns, cacheName)).To(Equal(int32(3)),
					"expected 3 replicas for static scaling")
				g.Expect(verifyConfigMapServerCount(ctx, ns, cacheName, 3)).To(Succeed(),
					"ConfigMap should reflect 3 servers for static scaling")
			}).Should(Succeed())
		})

		It("should have nil Scaling status for static scaling", func() {
			Eventually(func(g Gomega) {
				ss := getScalingStatus(ns, cacheName)
				g.Expect(ss).To(BeNil(),
					"Scaling should be nil for static scaling (no stabilization applies)")
			}).Should(Succeed())
		})

		It("should update replicas when static count is changed", func() {
			Eventually(func(g Gomega) {
				var cache storagev1.Cache
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: ns}, &cache)).To(Succeed())
				patch := client.MergeFrom(cache.DeepCopy())
				cache.Spec.Scaling.Static.NumServers = 5
				g.Expect(k8sClient.Patch(ctx, &cache, patch)).To(Succeed())
			}).Should(Succeed())

			Eventually(func(g Gomega) {
				g.Expect(getScalingReplicas(ns, cacheName)).To(Equal(int32(5)),
					"expected 5 replicas after static count update")
				g.Expect(verifyConfigMapServerCount(ctx, ns, cacheName, 5)).To(Succeed(),
					"ConfigMap should reflect 5 servers after static count update")
			}).Should(Succeed())
		})
	})

	Context("Conditions and events are set correctly", Ordered, func() {
		const ns = "scaling-conditions"
		cacheName := "cond-cache"

		BeforeAll(func() {
			ensureNamespace(ns)
			DeferCleanup(cleanupNamespace, ns)
			DeferCleanup(cleanupCache, ns, cacheName)
			DeferCleanup(cleanupAllNodes)

			By("creating 10 labeled nodes")
			for i := range 10 {
				node := createLabeledNode(fmt.Sprintf("cond-node-%d", i), map[string]string{selectorKey: selectorValue})
				Expect(k8sClient.Create(ctx, node)).To(Succeed())
			}
		})

		It("should set conditions and scaling status after initial reconciliation", func() {
			cache := testutil.NewDynamicCache(cacheName, ns, 50, &selectorKey, &selectorValue)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed())

			Eventually(func(g Gomega) {
				var c storagev1.Cache
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: ns}, &c)).To(Succeed())

				ready := getCondition(ns, cacheName, storagev1.ReadyCondition)
				g.Expect(ready).NotTo(BeNil(), "Ready condition should be set")

				replicas := getCondition(ns, cacheName, storagev1.ReplicasReadyCondition)
				g.Expect(replicas).NotTo(BeNil(), "ReplicasReady condition should be set")

				g.Expect(c.Status.Scaling).NotTo(BeNil(), "Scaling status should be set for dynamic scaling")
				g.Expect(c.Status.Scaling.StabilizationEnds).To(BeNil())
			}).Should(Succeed())
		})

		It("should set ReplicasReady=False when StatefulSet pods are not ready", func() {
			// In envtest there is no kubelet, so pods never become ready.
			// ReplicasReady should be False (StatefulSet has 0 ready replicas).
			Eventually(func(g Gomega) {
				cond := getCondition(ns, cacheName, storagev1.ReplicasReadyCondition)
				g.Expect(cond).NotTo(BeNil())
				g.Expect(cond.Status).To(Equal(metav1.ConditionFalse),
					"ReplicasReady should be False when no pods are ready")
			}).Should(Succeed())
		})

		It("should set Ready=False when ReplicasReady is False", func() {
			// Ready is an aggregate: False when any sub-condition is False.
			Eventually(func(g Gomega) {
				cond := getCondition(ns, cacheName, storagev1.ReadyCondition)
				g.Expect(cond).NotTo(BeNil())
				g.Expect(cond.Status).To(Equal(metav1.ConditionFalse),
					"Ready should be False when ReplicasReady is False")
			}).Should(Succeed())
		})

		It("should emit a Scaling event on initial scale-up", func() {
			Eventually(func(g Gomega) {
				var events eventsv1.EventList
				g.Expect(k8sClient.List(ctx, &events, client.InNamespace(ns))).To(Succeed())

				found := false
				for _, e := range events.Items {
					if e.Reason == storagev1.EventReasonScaling && e.Regarding.Name == cacheName {
						found = true
						g.Expect(e.Type).To(Equal(corev1.EventTypeNormal))
						g.Expect(e.Action).To(Equal(storagev1.EventActionScale))
						g.Expect(e.Note).To(ContainSubstring("5 replicas"))
					}
				}
				g.Expect(found).To(BeTrue(), "should find a Scaling event for the cache")
			}).Should(Succeed())
		})

		It("should emit a Scaling event when replica count changes", func() {
			By("changing percentage to 80% (8 replicas)")
			Eventually(func(g Gomega) {
				var cache storagev1.Cache
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: ns}, &cache)).To(Succeed())
				patch := client.MergeFrom(cache.DeepCopy())
				cache.Spec.Scaling.Dynamic.PercentageOfNodesToUse = 80
				g.Expect(k8sClient.Patch(ctx, &cache, patch)).To(Succeed())
			}).Should(Succeed())

			Eventually(func(g Gomega) {
				g.Expect(getScalingReplicas(ns, cacheName)).To(Equal(int32(8)))
			}).Should(Succeed())

			Eventually(func(g Gomega) {
				var events eventsv1.EventList
				g.Expect(k8sClient.List(ctx, &events, client.InNamespace(ns))).To(Succeed())

				found := false
				for _, e := range events.Items {
					if e.Reason == storagev1.EventReasonScaling && e.Regarding.Name == cacheName && e.Note != "" {
						if e.Action == storagev1.EventActionScale {
							// Look for the scale-up event from 5 to 8
							if containsAll(e.Note, "5", "8") {
								found = true
							}
						}
					}
				}
				g.Expect(found).To(BeTrue(), "should find a Scaling event for scale from 5 to 8")
			}).Should(Succeed())
		})
	})

	Context("When tolerance suppresses scaling, no event should fire", Ordered, func() {
		const ns = "scaling-no-event"
		cacheName := "noevent-cache"

		BeforeAll(func() {
			ensureNamespace(ns)
			DeferCleanup(cleanupNamespace, ns)
			DeferCleanup(cleanupCache, ns, cacheName)
			DeferCleanup(cleanupAllNodes)

			By("creating 20 labeled nodes")
			for i := range 20 {
				node := createLabeledNode(fmt.Sprintf("noevent-node-%d", i), map[string]string{selectorKey: selectorValue})
				Expect(k8sClient.Create(ctx, node)).To(Succeed())
			}
		})

		It("should scale to 50% of 20 nodes = 10 replicas", func() {
			cache := testutil.NewDynamicCache(cacheName, ns, 50, &selectorKey, &selectorValue)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed())

			Eventually(func(g Gomega) {
				g.Expect(getScalingReplicas(ns, cacheName)).To(Equal(int32(10)))
			}).Should(Succeed())
		})

		It("should NOT emit a new Scaling event when node change is within tolerance", func() {
			By("recording current event count")
			var beforeEvents eventsv1.EventList
			Expect(k8sClient.List(ctx, &beforeEvents, client.InNamespace(ns))).To(Succeed())
			scalingEventCount := 0
			for _, e := range beforeEvents.Items {
				if e.Reason == storagev1.EventReasonScaling && e.Regarding.Name == cacheName {
					scalingEventCount++
				}
			}

			By("deleting 1 node (19 nodes -> floor(9.5)=9, ratio 9/10=0.9 >= 1.0-0.1 -> suppressed)")
			node := &corev1.Node{}
			node.Name = "noevent-node-19"
			Expect(k8sClient.Delete(ctx, node)).To(Succeed())

			By("waiting for reconciliation and verifying no new event")
			Consistently(func() int {
				var events eventsv1.EventList
				Expect(k8sClient.List(ctx, &events, client.InNamespace(ns))).To(Succeed())
				count := 0
				for _, e := range events.Items {
					if e.Reason == storagev1.EventReasonScaling && e.Regarding.Name == cacheName {
						count++
					}
				}
				return count
			}, "3s", "200ms").Should(Equal(scalingEventCount),
				"no new Scaling event should fire when tolerance suppresses the change")
		})
	})

	Context("When switching from static to dynamic scaling", Ordered, func() {
		const ns = "scaling-transition"
		cacheName := "transition-cache"

		BeforeAll(func() {
			ensureNamespace(ns)
			DeferCleanup(cleanupNamespace, ns)
			DeferCleanup(cleanupCache, ns, cacheName)
			DeferCleanup(cleanupAllNodes)

			By("creating 10 labeled nodes")
			for i := range 10 {
				node := createLabeledNode(fmt.Sprintf("trans-node-%d", i), map[string]string{selectorKey: selectorValue})
				Expect(k8sClient.Create(ctx, node)).To(Succeed())
			}
		})

		It("should start with static scaling at 3 replicas", func() {
			cache := testutil.NewStaticCache(cacheName, ns, 3)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed())

			Eventually(func(g Gomega) {
				g.Expect(getScalingReplicas(ns, cacheName)).To(Equal(int32(3)))
			}).Should(Succeed())
		})

		It("should switch to dynamic scaling and scale immediately", func() {
			By("patching from static to dynamic 50% (10 nodes = 5 replicas)")
			Eventually(func(g Gomega) {
				var cache storagev1.Cache
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: ns}, &cache)).To(Succeed())
				patch := client.MergeFrom(cache.DeepCopy())
				cache.Spec.Scaling.Static = nil
				cache.Spec.Scaling.Dynamic = &storagev1.DynamicScaleConfig{
					PercentageOfNodesToUse: 50,
				}
				cache.Spec.NodeSelectorKey = &selectorKey
				cache.Spec.NodeSelectorValue = &selectorValue
				g.Expect(k8sClient.Patch(ctx, &cache, patch)).To(Succeed())
			}).Should(Succeed())

			By("verifying scale to 5 replicas (50% of 10 nodes)")
			Eventually(func(g Gomega) {
				g.Expect(getScalingReplicas(ns, cacheName)).To(Equal(int32(5)),
					"should scale to 5 after switching from static(3) to dynamic(50%%)")
			}).Should(Succeed())
		})
	})
})

// containsAll returns true if s contains all of the given substrings.
func containsAll(s string, subs ...string) bool {
	for _, sub := range subs {
		if !strings.Contains(s, sub) {
			return false
		}
	}
	return true
}
