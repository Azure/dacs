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
							Fixed: &storagev1.FixedScaleConfig{
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
				Client: k8sClient,
				Scheme: k8sClient.Scheme(),
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
		})
	})
})
