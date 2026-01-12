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
	"sigs.k8s.io/controller-runtime/pkg/reconcile"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

// test helper pointer constructors
func int32Ptr(v int32) *int32 { return &v }
func toBoolPtr(v bool) *bool  { return &v }
func strPtr(s string) *string { return &s }

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

	Context("When reconciling a resource", func() {
		const resourceName = "test-resource"
		const resourceNamespace = "azureml"

		ctx := context.Background()

		typeNamespacedName := types.NamespacedName{
			Name:      resourceName,
			Namespace: resourceNamespace,
		}
		cache := &storagev1.Cache{}

		BeforeEach(func() {
			By("create the namespace for the test resource")
			namespace := &corev1.Namespace{
				ObjectMeta: metav1.ObjectMeta{
					Name: resourceNamespace,
				},
			}
			err := k8sClient.Create(ctx, namespace)
			if err != nil && !errors.IsAlreadyExists(err) {
				Expect(err).NotTo(HaveOccurred())
			}

			By("creating the custom resource for the Kind Cache")
			err = k8sClient.Get(ctx, typeNamespacedName, cache)
			if err != nil && errors.IsNotFound(err) {
				resource := &storagev1.Cache{
					ObjectMeta: metav1.ObjectMeta{
						Name:      resourceName,
						Namespace: resourceNamespace,
					},
					Spec: storagev1.CacheSpec{
						Scaling: storagev1.ScalingType{
							Static: &storagev1.StaticScaleConfig{
								NumServers: 2,
							},
						},
						ServerSizeInGB: 100,
						ServerThreads:  16,
						CpuLimit:       int32Ptr(1),
						MemoryLimitMB:  int32Ptr(20480),
						Storage: &storagev1.StorageConfig{
							Ephemeral: toBoolPtr(true),
						},
						EgressBandwidthLimitGbps: int32Ptr(10),
						NodeSelectorKey:          strPtr("dacs.azure.com/processing-unit"),
						NodeSelectorValue:        strPtr("cpu"),
					},
				}
				Expect(k8sClient.Create(ctx, resource)).To(Succeed())
			}
		})

		AfterEach(func() {
			resource := &storagev1.Cache{}
			err := k8sClient.Get(ctx, typeNamespacedName, resource)
			Expect(err).NotTo(HaveOccurred())

			By("Cleanup the specific resource instance Cache")
			Expect(k8sClient.Delete(ctx, resource)).To(Succeed())
		})

		It("should successfully reconcile the resource", func() {
			By("Reconciling the created resource")
			controllerReconciler := &CacheReconciler{
				Client:           k8sClient,
				Scheme:           k8sClient.Scheme(),
				CacheServerImage: "cache-server:local",
			}

			_, err := controllerReconciler.Reconcile(ctx, reconcile.Request{
				NamespacedName: typeNamespacedName,
			})
			Expect(err).NotTo(HaveOccurred())

			// Cacheserver statefulset should be created by the reconciler
			timeout := 10 // seconds
			interval := 1 // second
			By("Verifying the CacheServer StatefulSet is created")
			Eventually(func() error {
				statefulSet := &appsv1.StatefulSet{}
				statefulSetName := types.NamespacedName{
					Name:      "cacheserver",
					Namespace: resourceNamespace,
				}
				return k8sClient.Get(ctx, statefulSetName, statefulSet)
			}, timeout, interval).Should(Succeed())

			// Cacheserver service should be created by the reconciler
			By("Verifying the CacheServer Service is created")
			Eventually(func() error {
				service := &corev1.Service{}
				serviceName := types.NamespacedName{
					Name:      "cacheserver",
					Namespace: resourceNamespace,
				}
				return k8sClient.Get(ctx, serviceName, service)
			}, timeout, interval).Should(Succeed())

			// Cacheserver discovery service should be created by the reconciler
			By("Verifying the CacheServer Discovery Service is created")
			Eventually(func() error {
				service := &corev1.Service{}
				serviceName := types.NamespacedName{
					Name:      "cacheserver-discovery",
					Namespace: resourceNamespace,
				}
				return k8sClient.Get(ctx, serviceName, service)
			}, timeout, interval).Should(Succeed())

			// Cacheserver discovery configmap should be created by the reconciler
			By("Verifying the CacheServer Discovery ConfigMap is created")
			Eventually(func() error {
				configMap := &corev1.ConfigMap{}
				configMapName := types.NamespacedName{
					Name:      "cacheserver-discovery-config",
					Namespace: resourceNamespace,
				}
				return k8sClient.Get(ctx, configMapName, configMap)
			}, timeout, interval).Should(Succeed())

			// Verify status conditions are set
			By("Verifying the Cache status has Ready condition")
			Eventually(func() bool {
				updatedCache := &storagev1.Cache{}
				err := k8sClient.Get(ctx, typeNamespacedName, updatedCache)
				if err != nil {
					return false
				}
				for _, condition := range updatedCache.Status.Conditions {
					if condition.Type == "Ready" {
						return true
					}
				}
				return false
			}, timeout, interval).Should(BeTrue())
		})

		It("should update StatefulSet when spec changes", func() {
			By("Reconciling the initial resource")
			controllerReconciler := &CacheReconciler{
				Client:           k8sClient,
				Scheme:           k8sClient.Scheme(),
				CacheServerImage: "cache-server:local",
			}

			_, err := controllerReconciler.Reconcile(ctx, reconcile.Request{
				NamespacedName: typeNamespacedName,
			})
			Expect(err).NotTo(HaveOccurred())

			By("Updating the Cache spec with new replica count")
			Eventually(func() error {
				cache := &storagev1.Cache{}
				if err := k8sClient.Get(ctx, typeNamespacedName, cache); err != nil {
					return err
				}
				cache.Spec.Scaling.Static.NumServers = 3
				return k8sClient.Update(ctx, cache)
			}).Should(Succeed())

			By("Reconciling after spec change")
			_, err = controllerReconciler.Reconcile(ctx, reconcile.Request{
				NamespacedName: typeNamespacedName,
			})
			Expect(err).NotTo(HaveOccurred())

			By("Verifying StatefulSet replicas are updated")
			timeout := 10
			interval := 1
			Eventually(func() int32 {
				statefulSet := &appsv1.StatefulSet{}
				statefulSetName := types.NamespacedName{
					Name:      "cacheserver",
					Namespace: resourceNamespace,
				}
				if err := k8sClient.Get(ctx, statefulSetName, statefulSet); err != nil {
					return 0
				}
				if statefulSet.Spec.Replicas == nil {
					return 0
				}
				return *statefulSet.Spec.Replicas
			}, timeout, interval).Should(Equal(int32(3)))
		})

		It("should update ConfigMap when server count changes", func() {
			By("Reconciling the initial resource")
			controllerReconciler := &CacheReconciler{
				Client:           k8sClient,
				Scheme:           k8sClient.Scheme(),
				CacheServerImage: "cache-server:local",
			}

			_, err := controllerReconciler.Reconcile(ctx, reconcile.Request{
				NamespacedName: typeNamespacedName,
			})
			Expect(err).NotTo(HaveOccurred())

			By("Verifying initial ConfigMap data")
			timeout := 10
			interval := 1
			Eventually(func() string {
				configMap := &corev1.ConfigMap{}
				configMapName := types.NamespacedName{
					Name:      "cacheserver-discovery-config",
					Namespace: resourceNamespace,
				}
				if err := k8sClient.Get(ctx, configMapName, configMap); err != nil {
					return ""
				}
				return configMap.Data["cache_server_count"]
			}, timeout, interval).Should(Equal("2"))

			By("Updating Cache spec with new server count")
			Eventually(func() error {
				cache := &storagev1.Cache{}
				if err := k8sClient.Get(ctx, typeNamespacedName, cache); err != nil {
					return err
				}
				cache.Spec.Scaling.Static.NumServers = 4
				return k8sClient.Update(ctx, cache)
			}).Should(Succeed())

			By("Reconciling after spec change")
			_, err = controllerReconciler.Reconcile(ctx, reconcile.Request{
				NamespacedName: typeNamespacedName,
			})
			Expect(err).NotTo(HaveOccurred())

			By("Verifying ConfigMap data is updated")
			Eventually(func() string {
				configMap := &corev1.ConfigMap{}
				configMapName := types.NamespacedName{
					Name:      "cacheserver-discovery-config",
					Namespace: resourceNamespace,
				}
				if err := k8sClient.Get(ctx, configMapName, configMap); err != nil {
					return ""
				}
				return configMap.Data["cache_server_count"]
			}, timeout, interval).Should(Equal("4"))
		})

		It("should handle missing Cache resource gracefully", func() {
			By("Reconciling a non-existent resource")
			controllerReconciler := &CacheReconciler{
				Client:           k8sClient,
				Scheme:           k8sClient.Scheme(),
				CacheServerImage: "cache-server:local",
			}

			nonExistent := types.NamespacedName{
				Name:      "non-existent",
				Namespace: resourceNamespace,
			}

			_, err := controllerReconciler.Reconcile(ctx, reconcile.Request{
				NamespacedName: nonExistent,
			})
			Expect(err).NotTo(HaveOccurred())
		})
	})
})
