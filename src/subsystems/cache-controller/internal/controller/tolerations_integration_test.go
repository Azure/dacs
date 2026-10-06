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
	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/utils/ptr"
	"sigs.k8s.io/controller-runtime/pkg/client"
)

var _ = Describe("Cache tolerations", func() {
	It("persists tolerations through admission, updates, reconciliation and StatefulSet recreation", func(ctx SpecContext) {
		cache := createTestCache("cache-tolerations", "default", 1, true)
		cache.Spec.Tolerations = []corev1.Toleration{{
			Key: "sku", Value: "gpu", Effect: corev1.TaintEffectNoSchedule,
		}}
		Expect(k8sClient.Create(ctx, cache)).To(Succeed(), "creating Cache %s", cache.Name)
		DeferCleanup(func(ctx SpecContext) {
			Expect(client.IgnoreNotFound(k8sClient.Delete(ctx, cache))).To(Succeed(), "deleting Cache %s", cache.Name)
		})
		key := client.ObjectKeyFromObject(cache)
		sts := &appsv1.StatefulSet{}
		expectTolerations := func(want []corev1.Toleration) {
			Eventually(func(g Gomega) {
				g.Expect(k8sClient.Get(ctx, key, sts)).To(Succeed(), "getting StatefulSet %s", key)
				if len(want) == 0 {
					g.Expect(sts.Spec.Template.Spec.Tolerations).To(BeEmpty(), "StatefulSet %s should have no tolerations", key)
				} else {
					g.Expect(sts.Spec.Template.Spec.Tolerations).To(Equal(want), "StatefulSet %s should use Cache tolerations", key)
				}
			}).WithContext(ctx).Should(Succeed(), "waiting for StatefulSet %s tolerations", key)
		}
		want := []corev1.Toleration{{
			Key: "sku", Operator: corev1.TolerationOpEqual, Value: "gpu", Effect: corev1.TaintEffectNoSchedule,
		}}
		expectTolerations(want)
		uid := sts.UID

		By("updating the Cache without replacing the StatefulSet")
		want[0].Effect = corev1.TaintEffectNoExecute
		want[0].TolerationSeconds = ptr.To(int64(60))
		Eventually(func(g Gomega) {
			g.Expect(k8sClient.Get(ctx, key, cache)).To(Succeed(), "getting Cache %s", key)
			cache.Spec.Tolerations = want
			g.Expect(k8sClient.Update(ctx, cache)).To(Succeed(), "updating Cache %s tolerations", key)
		}).WithContext(ctx).Should(Succeed(), "waiting for Cache %s update", key)
		expectTolerations(want)
		Expect(sts.UID).To(Equal(uid), "toleration update should preserve StatefulSet identity")
		Expect(sts.Spec.UpdateStrategy.Type).To(Equal(appsv1.RollingUpdateStatefulSetStrategyType), "toleration update should use rolling updates")

		By("restoring tolerations after a direct StatefulSet edit")
		Eventually(func(g Gomega) {
			g.Expect(k8sClient.Get(ctx, key, sts)).To(Succeed(), "getting StatefulSet %s", key)
			sts.Spec.Template.Spec.Tolerations = nil
			g.Expect(k8sClient.Update(ctx, sts)).To(Succeed(), "clearing StatefulSet %s tolerations", key)
		}).WithContext(ctx).Should(Succeed(), "waiting for direct StatefulSet %s edit", key)
		expectTolerations(want)

		By("recreating a deleted StatefulSet from the persisted Cache")
		Expect(k8sClient.Delete(ctx, sts)).To(Succeed(), "deleting StatefulSet %s", key)
		Eventually(func(g Gomega) {
			g.Expect(k8sClient.Get(ctx, key, sts)).To(Succeed(), "getting recreated StatefulSet %s", key)
			g.Expect(sts.UID).NotTo(Equal(uid), "recreated StatefulSet should have a new identity")
		}).WithContext(ctx).Should(Succeed(), "waiting for StatefulSet %s recreation", key)
		expectTolerations(want)

		By("rejecting an invalid update before it reaches the workload")
		Expect(k8sClient.Get(ctx, key, cache)).To(Succeed(), "getting Cache %s", key)
		cache.Spec.Tolerations = []corev1.Toleration{{Key: "sku", Operator: "Invalid"}}
		Expect(k8sClient.Update(ctx, cache)).NotTo(Succeed(), "invalid Cache tolerations should fail admission")
		expectTolerations(want)

		By("removing the Cache tolerations")
		Eventually(func(g Gomega) {
			g.Expect(k8sClient.Get(ctx, key, cache)).To(Succeed(), "getting Cache %s", key)
			cache.Spec.Tolerations = nil
			g.Expect(k8sClient.Update(ctx, cache)).To(Succeed(), "removing Cache %s tolerations", key)
		}).WithContext(ctx).Should(Succeed(), "waiting for Cache %s toleration removal", key)
		expectTolerations(nil)
	})

	It("rejects invalid tolerations at admission", func(ctx SpecContext) {
		cache := createTestCache("invalid-tolerations", "default", 1, true)
		cache.Spec.Tolerations = []corev1.Toleration{{
			Key: "sku", Operator: corev1.TolerationOpExists, Value: "gpu",
		}}
		Expect(k8sClient.Create(ctx, cache)).NotTo(Succeed(), "invalid tolerations should reject Cache creation")
		created := &storagev1.Cache{}
		Expect(k8sClient.Get(ctx, client.ObjectKeyFromObject(cache), created)).NotTo(Succeed(), "rejected Cache should not be persisted")
	})
})
