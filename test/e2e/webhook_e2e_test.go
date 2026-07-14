// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

package e2e

import (
	"context"
	"time"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/types"
)

var _ = Describe("Webhook", Label("e2e", "controller"), Ordered, func() {
	It("should apply defaults via mutating webhook", func(ctx SpecContext) {
		cache := &storagev1.Cache{
			ObjectMeta: metav1.ObjectMeta{
				Name:      "e2e-webhook-mutating",
				Namespace: namespace,
			},
			Spec: storagev1.CacheSpec{
				Scaling: storagev1.ScalingType{
					Dynamic: &storagev1.DynamicScaleConfig{
						PercentageOfNodesToUse: 50,
					},
				},
				ServerSizeInGB: 100,
				ServerThreads:  4,
			},
		}
		Expect(k8sClient.Create(ctx, cache)).To(Succeed(), "should create Cache CR via mutating webhook")
		DeferCleanup(func(ctx context.Context) {
			_ = k8sClient.Delete(ctx, cache)
			Eventually(func() bool {
				return errors.IsNotFound(k8sClient.Get(ctx, types.NamespacedName{Name: cache.Name, Namespace: namespace}, cache))
			}, 30*time.Second, time.Second).Should(BeTrue(), "Cache CR should be deleted")
		})

		Expect(k8sClient.Get(ctx, types.NamespacedName{Name: cache.Name, Namespace: namespace}, cache)).To(Succeed(), "should fetch created Cache CR")
		Expect(cache.Spec.Eviction).NotTo(BeNil(), "eviction should be defaulted")
		Expect(cache.Spec.Storage).NotTo(BeNil(), "storage should be defaulted")
		Expect(cache.Spec.Port).NotTo(BeNil(), "port should be defaulted")
		Expect(*cache.Spec.Port).To(Equal(storagev1.DefaultPort))
	})

	It("should reject invalid input via validating webhook", func(ctx SpecContext) {
		cache := &storagev1.Cache{
			ObjectMeta: metav1.ObjectMeta{
				Name:      "e2e-webhook-validating",
				Namespace: namespace,
			},
			Spec: storagev1.CacheSpec{
				Scaling: storagev1.ScalingType{
					Dynamic: &storagev1.DynamicScaleConfig{
						PercentageOfNodesToUse: 150,
					},
				},
				ServerSizeInGB: 100,
				ServerThreads:  4,
			},
		}
		err := k8sClient.Create(ctx, cache)
		Expect(err).To(HaveOccurred(), "validating webhook should reject invalid percentage")

		statusErr, ok := err.(*errors.StatusError)
		Expect(ok).To(BeTrue(), "error should be a StatusError")
		Expect(statusErr.Status().Reason).To(Equal(metav1.StatusReasonInvalid), "error reason should be Invalid")
		Expect(statusErr.Status().Details).NotTo(BeNil(), "error should have details")
		Expect(statusErr.Status().Details.Causes).NotTo(BeEmpty(), "error should have causes")
		Expect(statusErr.Status().Details.Causes[0].Field).To(
			Equal("spec.scaling.dynamic.percentageOfNodesToUse"),
			"rejection should reference the percentageOfNodesToUse field")
	})
})
