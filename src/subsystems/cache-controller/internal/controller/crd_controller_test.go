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

package controller

import (
	"context"
	"fmt"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/api/meta"
	"k8s.io/apimachinery/pkg/types"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

// test helper pointer constructors
func int32Ptr(v int32) *int32 { return &v }
func toBoolPtr(v bool) *bool  { return &v }
func strPtr(s string) *string { return &s }

// createTestCache creates a Cache CR with the given name and number of replicas
func createTestCache(name, namespace string, numServers int32, useEphemeral bool) *storagev1.Cache {
	var storageConfig *storagev1.StorageConfig
	if useEphemeral {
		storageConfig = &storagev1.StorageConfig{
			Ephemeral: toBoolPtr(true),
		}
	}
	return &storagev1.Cache{
		ObjectMeta: metav1.ObjectMeta{
			Name:      name,
			Namespace: namespace,
		},
		Spec: storagev1.CacheSpec{
			Scaling: storagev1.ScalingType{
				Static: &storagev1.StaticScaleConfig{
					NumServers: numServers,
				},
			},
			ServerSizeInGB: 100,
			ServerThreads:  16,
			CpuLimit:       int32Ptr(1),
			MemoryLimitMB:  int32Ptr(20480),
			Storage:        storageConfig,
		},
	}
}

var _ = Describe("Cache Controller", func() {
	Context("When reconciling a resource", Ordered, func() {
		const resourceNamespace = "azureml"
		ctx := context.Background()
		timeout := 10     // seconds
		pollInterval := 1 // second

		// Helper to get StatefulSet by cache name
		getStatefulSetByName := func(name string) *appsv1.StatefulSet {
			statefulSet := &appsv1.StatefulSet{}
			err := k8sClient.Get(ctx, types.NamespacedName{
				Name:      name,
				Namespace: resourceNamespace,
			}, statefulSet)
			if err != nil || statefulSet.Spec.Replicas == nil {
				return nil
			}
			return statefulSet
		}
		// Helper to get StatefulSet replicas by cache name
		getStatefulSetReplicasByName := func(name string) int32 {
			ss := getStatefulSetByName(name)
			if ss == nil {
				return -1
			}
			return *ss.Spec.Replicas
		}
		// Helper to get StatefulSet scheduler name by cache name
		getStatefulSetSchedulerByName := func(name string) string {
			ss := getStatefulSetByName(name)
			if ss == nil {
				return ""
			}
			return ss.Spec.Template.Spec.SchedulerName
		}

		// Helper to get env var value from the first container of the StatefulSet.
		// Must only be called after the STS is confirmed to exist (via prior Eventually checks).
		getStatefulSetEnvVar := func(name, envName string) (string, bool) {
			statefulSet := &appsv1.StatefulSet{}
			err := k8sClient.Get(ctx, types.NamespacedName{
				Name:      name,
				Namespace: resourceNamespace,
			}, statefulSet)
			Expect(err).NotTo(HaveOccurred(), "StatefulSet should already exist at this point")
			for _, env := range statefulSet.Spec.Template.Spec.Containers[0].Env {
				if env.Name == envName {
					return env.Value, true
				}
			}
			return "", false
		}

		// Helper to get discovery ConfigMap server count by cache name
		getConfigMapServerCountByName := func(name string) string {
			configMap := &corev1.ConfigMap{}
			err := k8sClient.Get(ctx, types.NamespacedName{
				Name:      name + storagev1.DiscoveryConfigSuffix,
				Namespace: resourceNamespace,
			}, configMap)
			if err != nil {
				return ""
			}
			return configMap.Data["cache_server_count"]
		}

		// Helper to check if service exists
		serviceExists := func(name string) error {
			service := &corev1.Service{}
			return k8sClient.Get(ctx, types.NamespacedName{
				Name:      name,
				Namespace: resourceNamespace,
			}, service)
		}

		// Helper to update Cache replica count
		updateCacheReplicas := func(name string, replicas int32) error {
			cache := &storagev1.Cache{}
			if err := k8sClient.Get(ctx, types.NamespacedName{
				Name:      name,
				Namespace: resourceNamespace,
			}, cache); err != nil {
				return err
			}
			cache.Spec.Scaling.Static.NumServers = replicas
			return k8sClient.Update(ctx, cache)
		}

		BeforeAll(func() {
			By("creating the namespace for the test resources")
			namespace := &corev1.Namespace{
				ObjectMeta: metav1.ObjectMeta{
					Name: resourceNamespace,
				},
			}
			err := k8sClient.Create(ctx, namespace)
			if err != nil && !errors.IsAlreadyExists(err) {
				Expect(err).NotTo(HaveOccurred())
			}
		})

		// deleteCache deletes a Cache CR and waits for cleanup
		deleteCache := func(name string) {
			cache := &storagev1.Cache{}
			err := k8sClient.Get(ctx, types.NamespacedName{Name: name, Namespace: resourceNamespace}, cache)
			if err == nil {
				_ = k8sClient.Delete(ctx, cache)

				// Delete subresources created by the Cache CR
				sts := getStatefulSetByName(name)
				if sts != nil {
					_ = k8sClient.Delete(ctx, sts)
				}

				svc := &corev1.Service{}
				err = k8sClient.Get(ctx, types.NamespacedName{Name: name, Namespace: resourceNamespace}, svc)
				if err == nil {
					_ = k8sClient.Delete(ctx, svc)
				}

				discoverySvc := &corev1.Service{}
				err = k8sClient.Get(ctx, types.NamespacedName{Name: name + storagev1.DiscoverySuffix, Namespace: resourceNamespace}, discoverySvc)
				if err == nil {
					_ = k8sClient.Delete(ctx, discoverySvc)
				}

				cm := &corev1.ConfigMap{}
				err = k8sClient.Get(ctx, types.NamespacedName{Name: name + storagev1.DiscoveryConfigSuffix, Namespace: resourceNamespace}, cm)
				if err == nil {
					_ = k8sClient.Delete(ctx, cm)
				}
			}
		}

		It("should create and update resources when Cache CR with ephemeral storage is created and modified", func() {
			cacheName := "test-cache"

			By("creating Cache CR with 2 replicas")
			cache := createTestCache(cacheName, resourceNamespace, 2, true)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed())

			By("verifying the CacheServer StatefulSet is created with correct replicas")
			Eventually(func() int32 { return getStatefulSetReplicasByName(cacheName) }, timeout, pollInterval).Should(Equal(int32(2)))

			By("Verifying the CacheServer StatefulSet has default-scheduler annotation")
			Eventually(func() string { return getStatefulSetSchedulerByName(cacheName) }, timeout, pollInterval).Should(Equal("default-scheduler"))

			By("verifying CACHESERVER_USE_ZEROCOPY is disabled (feature gate off by default)")
			// STS already confirmed to exist by prior Eventually checks.
			// Env var absent or set to "0" both mean disabled.
			val, found := getStatefulSetEnvVar(cacheName, "CACHESERVER_USE_ZEROCOPY")
			if found {
				Expect(val).To(Equal("0"), "CACHESERVER_USE_ZEROCOPY should be '0' when feature gate is disabled")
			}

			By("verifying the CacheServer Service is created")
			Eventually(func() error { return serviceExists(cacheName) }, timeout, pollInterval).Should(Succeed())

			By("verifying the CacheServer Discovery Service is created")
			Eventually(func() error { return serviceExists(cacheName + storagev1.DiscoverySuffix) }, timeout, pollInterval).Should(Succeed())

			By("verifying the CacheServer Discovery ConfigMap is created with correct server count")
			Eventually(func() string { return getConfigMapServerCountByName(cacheName) }, timeout, pollInterval).Should(Equal("2"))

			By("verifying webhook defaults were applied to the Cache CR")
			updatedCache := &storagev1.Cache{}
			Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: resourceNamespace}, updatedCache)).To(Succeed(), "should get Cache CR")
			Expect(updatedCache.Spec.Port).NotTo(BeNil(), "webhook should set default Port")
			Expect(*updatedCache.Spec.Port).To(Equal(storagev1.DefaultPort), "Port should equal DefaultPort")
			Expect(updatedCache.Spec.MetricsPort).NotTo(BeNil(), "webhook should set default MetricsPort")
			Expect(*updatedCache.Spec.MetricsPort).To(Equal(storagev1.DefaultMetricsPort), "MetricsPort should equal DefaultMetricsPort")
			Expect(updatedCache.Spec.NodeSelectorKey).NotTo(BeNil(), "webhook should set default NodeSelectorKey")
			Expect(*updatedCache.Spec.NodeSelectorKey).To(Equal(storagev1.DefaultNodeSelectorKey), "NodeSelectorKey should equal DefaultNodeSelectorKey")
			Expect(updatedCache.Spec.Storage).NotTo(BeNil(), "webhook should set default Storage")
			Expect(updatedCache.Spec.Storage.Ephemeral).NotTo(BeNil(), "webhook should set default Ephemeral")
			Expect(*updatedCache.Spec.Storage.Ephemeral).To(BeTrue(), "Ephemeral should be true as set in test")

			By("Verifying that cache status has discovery URL set")
			Eventually(func() string {
				updatedCache := &storagev1.Cache{}
				err := k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: resourceNamespace}, updatedCache)
				if err != nil {
					return ""
				}
				return updatedCache.Status.DiscoveryURL
			}, timeout, pollInterval).Should(Equal(fmt.Sprintf("%s-discovery.%s.svc.cluster.local", cacheName, resourceNamespace)))

			By("verifying the Cache status has Ready condition")
			Eventually(func() bool {
				updatedCache := &storagev1.Cache{}
				err := k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: resourceNamespace}, updatedCache)
				if err != nil {
					return false
				}
				for _, condition := range updatedCache.Status.Conditions {
					if condition.Type == "Ready" {
						return true
					}
				}
				return false
			}, timeout, pollInterval).Should(BeTrue())

			By("verifying the Cache status has all expected conditions")
			Eventually(func(g Gomega) {
				cache := &storagev1.Cache{}
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: resourceNamespace}, cache)).To(Succeed())
				g.Expect(meta.FindStatusCondition(cache.Status.Conditions, "ReplicasReady")).NotTo(BeNil())
				g.Expect(meta.IsStatusConditionTrue(cache.Status.Conditions, "PodsScheduled")).To(BeTrue())
			}, timeout, pollInterval).Should(Succeed())

			By("updating Cache CR to 4 replicas (scaling up)")
			Eventually(func() error { return updateCacheReplicas(cacheName, 4) }).Should(Succeed())

			By("verifying StatefulSet scales to 4 replicas")
			Eventually(func() int32 { return getStatefulSetReplicasByName(cacheName) }, timeout, pollInterval).Should(Equal(int32(4)))

			By("verifying ConfigMap is updated with server count of 4")
			Eventually(func() string { return getConfigMapServerCountByName(cacheName) }, timeout, pollInterval).Should(Equal("4"))

			By("updating Cache CR to 3 replicas (scaling down)")
			Eventually(func() error { return updateCacheReplicas(cacheName, 3) }).Should(Succeed())

			By("verifying StatefulSet scales down to 3 replicas")
			Eventually(func() int32 { return getStatefulSetReplicasByName(cacheName) }, timeout, pollInterval).Should(Equal(int32(3)))

			By("verifying ConfigMap is updated with server count of 3")
			Eventually(func() string { return getConfigMapServerCountByName(cacheName) }, timeout, pollInterval).Should(Equal("3"))

			By("cleaning up test-cache before next test")
			deleteCache(cacheName)
		})

		It("should grant NET_ADMIN only when egress bandwidth limiting is configured", func(ctx SpecContext) {
			const cacheName = "test-cache-traffic-control"
			cache := createTestCache(cacheName, resourceNamespace, 1, true)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed(), "creating Cache CR without an egress limit")
			DeferCleanup(func(cleanupCtx SpecContext) {
				Expect(k8sClient.Delete(cleanupCtx, cache)).To(Succeed(), "deleting traffic-control test Cache CR")
			})

			Eventually(func(g Gomega) {
				statefulSet := &appsv1.StatefulSet{}
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: resourceNamespace}, statefulSet)).
					To(Succeed(), "getting CacheServer StatefulSet without an egress limit")
				g.Expect(statefulSet.Spec.Template.Spec.Containers[0].SecurityContext).
					To(BeNil(), "security context should be absent without an egress limit")
			}, timeout, pollInterval).Should(Succeed(), "waiting for the unprivileged CacheServer StatefulSet")

			Eventually(func() error {
				updatedCache := &storagev1.Cache{}
				if err := k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: resourceNamespace}, updatedCache); err != nil {
					return err
				}
				updatedCache.Spec.EgressBandwidthLimitGbps = int32Ptr(1)
				return k8sClient.Update(ctx, updatedCache)
			}, timeout, pollInterval).Should(Succeed(), "setting the Cache CR egress bandwidth limit")

			Eventually(func(g Gomega) {
				statefulSet := &appsv1.StatefulSet{}
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: resourceNamespace}, statefulSet)).
					To(Succeed(), "getting CacheServer StatefulSet with an egress limit")
				g.Expect(statefulSet.Spec.Template.Spec.Containers[0].SecurityContext).
					NotTo(BeNil(), "security context should be present with an egress limit")
				g.Expect(statefulSet.Spec.Template.Spec.Containers[0].SecurityContext.Capabilities.Add).
					To(ContainElement(corev1.Capability("NET_ADMIN")), "NET_ADMIN should be granted with an egress limit")
			}, timeout, pollInterval).Should(Succeed(), "waiting for the privileged CacheServer StatefulSet")
		})

		It("should create and update resources when Cache CR with persistent storage is created and modified", func() {
			cacheName := "test-cache-persistent"

			By("ensuring no leftover Cache CR exists")
			deleteCache("test-cache")

			By("creating Cache CR with 3 replicas")
			cache := createTestCache(cacheName, resourceNamespace, 3, false)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed())
			DeferCleanup(func() { _ = k8sClient.Delete(ctx, cache) })

			By("verifying the CacheServer StatefulSet is created with correct replicas")
			Eventually(func() int32 { return getStatefulSetReplicasByName(cacheName) }, timeout, pollInterval).Should(Equal(int32(3)))

			By("verifying the CacheServer Statefulset uses custom scheduler cache-controller")
			Eventually(func() string { return getStatefulSetSchedulerByName(cacheName) }, timeout, pollInterval).Should(Equal("cache-controller"))

			By("verifying the CacheServer Service is created")
			Eventually(func() error { return serviceExists(cacheName) }, timeout, pollInterval).Should(Succeed())

			By("verifying the CacheServer Discovery Service is created")
			Eventually(func() error { return serviceExists(cacheName + storagev1.DiscoverySuffix) }, timeout, pollInterval).Should(Succeed())

			By("verifying the CacheServer Discovery ConfigMap is created with correct server count")
			Eventually(func() string { return getConfigMapServerCountByName(cacheName) }, timeout, pollInterval).Should(Equal("3"))

			By("verifying the CacheServer Discovery URL is set in the status")
			Eventually(func() string {
				updatedCache := &storagev1.Cache{}
				err := k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: resourceNamespace}, updatedCache)
				if err != nil {
					return ""
				}
				return updatedCache.Status.DiscoveryURL
			}, timeout, pollInterval).Should(Equal(fmt.Sprintf("%s-discovery.%s.svc.cluster.local", cacheName, resourceNamespace)))

			By("verifying status conditions")
			Eventually(func(g Gomega) {
				cache := &storagev1.Cache{}
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: resourceNamespace}, cache)).To(Succeed())
				g.Expect(meta.FindStatusCondition(cache.Status.Conditions, "Ready")).NotTo(BeNil())
				g.Expect(meta.FindStatusCondition(cache.Status.Conditions, "ReplicasReady")).NotTo(BeNil())
				// Persistent pods use schedulerName: cache-controller. In envtest
				// there is no STS controller creating pods, so the scheduling
				// reconciler sees zero pods and reports all scheduled.
				g.Expect(meta.IsStatusConditionTrue(cache.Status.Conditions, "PodsScheduled")).To(BeTrue())
			}, timeout, pollInterval).Should(Succeed())
		})
	})

	Context("Multi-cache in same namespace", Ordered, func() {
		const resourceNamespace = "azureml-multi"
		timeout := 10     // seconds
		pollInterval := 1 // second
		BeforeAll(func() {
			By("creating the namespace for multi-cache tests")
			ctx := context.Background()
			namespace := corev1.Namespace{
				ObjectMeta: metav1.ObjectMeta{
					Name: resourceNamespace,
				},
			}
			err := k8sClient.Create(ctx, &namespace)
			if err != nil && !errors.IsAlreadyExists(err) {
				Expect(err).NotTo(HaveOccurred())
			}
		})

		It("should create isolated resources for two Cache CRs in the same namespace", func(ctx context.Context) {
			cacheA := "cache-alpha"
			cacheB := "cache-beta"

			By("creating two Cache CRs with different names")
			crA := createTestCache(cacheA, resourceNamespace, 2, true)
			crB := createTestCache(cacheB, resourceNamespace, 3, true)
			Expect(k8sClient.Create(ctx, crA)).To(Succeed())
			Expect(k8sClient.Create(ctx, crB)).To(Succeed())
			DeferCleanup(func() {
				_ = k8sClient.Delete(ctx, crA)
				_ = k8sClient.Delete(ctx, crB)
			})

			By("verifying each cache gets its own StatefulSet with correct replicas")
			Eventually(func() int32 {
				sts := &appsv1.StatefulSet{}
				if err := k8sClient.Get(ctx, types.NamespacedName{Name: cacheA, Namespace: resourceNamespace}, sts); err != nil || sts.Spec.Replicas == nil {
					return -1
				}
				return *sts.Spec.Replicas
			}).Should(Equal(int32(2)))

			Eventually(func() int32 {
				sts := &appsv1.StatefulSet{}
				if err := k8sClient.Get(ctx, types.NamespacedName{Name: cacheB, Namespace: resourceNamespace}, sts); err != nil || sts.Spec.Replicas == nil {
					return -1
				}
				return *sts.Spec.Replicas
			}).Should(Equal(int32(3)))

			By("verifying StatefulSets have distinct cache-name labels")
			var stsA appsv1.StatefulSet
			Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheA, Namespace: resourceNamespace}, &stsA)).To(Succeed())
			Expect(stsA.Spec.Selector.MatchLabels[CacheNameLabelKey]).To(Equal(cacheA))

			var stsB appsv1.StatefulSet
			Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cacheB, Namespace: resourceNamespace}, &stsB)).To(Succeed())
			Expect(stsB.Spec.Selector.MatchLabels[CacheNameLabelKey]).To(Equal(cacheB))

			By("verifying each cache gets its own headless Service")
			var svcA corev1.Service
			Eventually(func() error {
				return k8sClient.Get(ctx, types.NamespacedName{Name: cacheA, Namespace: resourceNamespace}, &svcA)
			}).Should(Succeed())
			Expect(svcA.Spec.Selector[CacheNameLabelKey]).To(Equal(cacheA))

			var svcB corev1.Service
			Eventually(func() error {
				return k8sClient.Get(ctx, types.NamespacedName{Name: cacheB, Namespace: resourceNamespace}, &svcB)
			}, timeout, pollInterval).Should(Succeed())
			Expect(svcB.Spec.Selector[CacheNameLabelKey]).To(Equal(cacheB))

			By("verifying each cache gets its own discovery Service")
			var discA corev1.Service
			Eventually(func() error {
				return k8sClient.Get(ctx, types.NamespacedName{Name: cacheA + storagev1.DiscoverySuffix, Namespace: resourceNamespace}, &discA)
			}, timeout, pollInterval).Should(Succeed())
			Expect(discA.Spec.Selector[CacheNameLabelKey]).To(Equal(cacheA))

			var discB corev1.Service
			Eventually(func() error {
				return k8sClient.Get(ctx, types.NamespacedName{Name: cacheB + storagev1.DiscoverySuffix, Namespace: resourceNamespace}, &discB)
			}, timeout, pollInterval).Should(Succeed())
			Expect(discB.Spec.Selector[CacheNameLabelKey]).To(Equal(cacheB))

			By("verifying each cache gets its own discovery ConfigMap")
			var cmA corev1.ConfigMap
			Eventually(func() string {
				if err := k8sClient.Get(ctx, types.NamespacedName{Name: cacheA + storagev1.DiscoveryConfigSuffix, Namespace: resourceNamespace}, &cmA); err != nil {
					return ""
				}
				return cmA.Data["cache_server_count"]
			}, timeout, pollInterval).Should(Equal("2"))

			var cmB corev1.ConfigMap
			Eventually(func() string {
				if err := k8sClient.Get(ctx, types.NamespacedName{Name: cacheB + storagev1.DiscoveryConfigSuffix, Namespace: resourceNamespace}, &cmB); err != nil {
					return ""
				}
				return cmB.Data["cache_server_count"]
			}, timeout, pollInterval).Should(Equal("3"))
		})
	})
})
