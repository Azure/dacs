/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package controller

import (
	"context"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"k8s.io/apimachinery/pkg/api/errors"
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
func createTestCache(name, namespace string, numServers int32) *storagev1.Cache {
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
			Storage: &storagev1.StorageConfig{
				Ephemeral: toBoolPtr(true),
			},
		},
	}
}

var _ = Describe("Cache Controller", func() {
	Describe("Helper Functions", func() {
		Describe("getPortWithDefault", func() {
			It("should return default when port is nil", func() {
				result := getPortWithDefault(nil, 8080)
				Expect(result).To(Equal(int32(8080)))
			})

			It("should return provided value when port is not nil", func() {
				port := int32(9000)
				result := getPortWithDefault(&port, 8080)
				Expect(result).To(Equal(int32(9000)))
			})
		})

		Describe("boolValueOrDefault", func() {
			It("should return default when value is nil", func() {
				result := boolValueOrDefault(nil, true)
				Expect(result).To(BeTrue())
			})

			It("should return provided value when not nil", func() {
				value := false
				result := boolValueOrDefault(&value, true)
				Expect(result).To(BeFalse())
			})
		})

		Describe("setStatusCondition", func() {
			var status *storagev1.CacheStatus

			BeforeEach(func() {
				status = &storagev1.CacheStatus{
					Conditions: []metav1.Condition{},
				}
			})

			It("should add new condition when none exists", func() {
				condition := metav1.Condition{
					Type:    "Ready",
					Status:  metav1.ConditionTrue,
					Reason:  "TestReason",
					Message: "Test message",
				}
				setStatusCondition(status, condition)

				Expect(status.Conditions).To(HaveLen(1))
				Expect(status.Conditions[0].Type).To(Equal("Ready"))
				Expect(status.Conditions[0].Status).To(Equal(metav1.ConditionTrue))
			})

			It("should update existing condition with same type", func() {
				initial := metav1.Condition{
					Type:    "Ready",
					Status:  metav1.ConditionFalse,
					Reason:  "InitialReason",
					Message: "Initial message",
				}
				setStatusCondition(status, initial)

				updated := metav1.Condition{
					Type:    "Ready",
					Status:  metav1.ConditionTrue,
					Reason:  "UpdatedReason",
					Message: "Updated message",
				}
				setStatusCondition(status, updated)

				Expect(status.Conditions).To(HaveLen(1))
				Expect(status.Conditions[0].Status).To(Equal(metav1.ConditionTrue))
				Expect(status.Conditions[0].Reason).To(Equal("UpdatedReason"))
			})

			It("should preserve LastTransitionTime when status unchanged", func() {
				initial := metav1.Condition{
					Type:               "Ready",
					Status:             metav1.ConditionTrue,
					Reason:             "InitialReason",
					Message:            "Initial message",
					LastTransitionTime: metav1.Now(),
				}
				setStatusCondition(status, initial)
				originalTime := status.Conditions[0].LastTransitionTime

				updated := metav1.Condition{
					Type:    "Ready",
					Status:  metav1.ConditionTrue,
					Reason:  "SameStatus",
					Message: "Status unchanged",
				}
				setStatusCondition(status, updated)

				Expect(status.Conditions[0].LastTransitionTime).To(Equal(originalTime))
			})
		})
	})

	Context("When reconciling a resource", Ordered, func() {
		const resourceNamespace = "azureml"
		ctx := context.Background()
		timeout := 10     // seconds
		pollInterval := 1 // second

		// Helper to get StatefulSet replicas
		getStatefulSetReplicas := func() int32 {
			statefulSet := &appsv1.StatefulSet{}
			err := k8sClient.Get(ctx, types.NamespacedName{
				Name:      "cacheserver",
				Namespace: resourceNamespace,
			}, statefulSet)
			if err != nil || statefulSet.Spec.Replicas == nil {
				return 0
			}
			return *statefulSet.Spec.Replicas
		}

		// Helper to get discovery ConfigMap server count
		getConfigMapServerCount := func() string {
			configMap := &corev1.ConfigMap{}
			err := k8sClient.Get(ctx, types.NamespacedName{
				Name:      "cacheserver-discovery-config",
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

		It("should create and update resources when Cache CR is created and modified", func() {
			cacheName := "test-cache"

			By("creating Cache CR with 2 replicas")
			cache := createTestCache(cacheName, resourceNamespace, 2)
			Expect(k8sClient.Create(ctx, cache)).To(Succeed())
			DeferCleanup(func() { _ = k8sClient.Delete(ctx, cache) })

			By("verifying the CacheServer StatefulSet is created with correct replicas")
			Eventually(getStatefulSetReplicas, timeout, pollInterval).Should(Equal(int32(2)))

			By("verifying the CacheServer Service is created")
			Eventually(func() error { return serviceExists("cacheserver") }, timeout, pollInterval).Should(Succeed())

			By("verifying the CacheServer Discovery Service is created")
			Eventually(func() error { return serviceExists("cacheserver-discovery") }, timeout, pollInterval).Should(Succeed())

			By("verifying the CacheServer Discovery ConfigMap is created with correct server count")
			Eventually(getConfigMapServerCount, timeout, pollInterval).Should(Equal("2"))

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

			By("updating Cache CR to 4 replicas (scaling up)")
			Eventually(func() error { return updateCacheReplicas(cacheName, 4) }).Should(Succeed())

			By("verifying StatefulSet scales to 4 replicas")
			Eventually(getStatefulSetReplicas, timeout, pollInterval).Should(Equal(int32(4)))

			By("verifying ConfigMap is updated with server count of 4")
			Eventually(getConfigMapServerCount, timeout, pollInterval).Should(Equal("4"))

			By("updating Cache CR to 3 replicas (scaling down)")
			Eventually(func() error { return updateCacheReplicas(cacheName, 3) }).Should(Succeed())

			By("verifying StatefulSet scales down to 3 replicas")
			Eventually(getStatefulSetReplicas, timeout, pollInterval).Should(Equal(int32(3)))

			By("verifying ConfigMap is updated with server count of 3")
			Eventually(getConfigMapServerCount, timeout, pollInterval).Should(Equal("3"))
		})
	})
})
