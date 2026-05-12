/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package controller

import (
	"context"
	"errors"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	eventsv1 "k8s.io/api/events/v1"
	apierrors "k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/types"
	"k8s.io/utils/ptr"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/controller/controllerutil"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
)

const (
	legacyTestNamespace = "azureml-legacy"
)

// legacyMinimalSTS builds a StatefulSet that is just valid enough for the
// envtest API server to accept on Create. It uses OrderedReady pod
// management so the controller's reconcile path that handles immutable
// field mismatches kicks in.
func legacyMinimalSTS(name, namespace string) *appsv1.StatefulSet {
	labels := map[string]string{CacheNameLabelKey: name}
	return &appsv1.StatefulSet{
		ObjectMeta: metav1.ObjectMeta{
			Name:      name,
			Namespace: namespace,
			Labels:    labels,
		},
		Spec: appsv1.StatefulSetSpec{
			Replicas:            ptr.To(int32(1)),
			ServiceName:         name,
			Selector:            &metav1.LabelSelector{MatchLabels: labels},
			PodManagementPolicy: appsv1.OrderedReadyPodManagement,
			Template: corev1.PodTemplateSpec{
				ObjectMeta: metav1.ObjectMeta{Labels: labels},
				Spec: corev1.PodSpec{
					Containers: []corev1.Container{{
						Name:  "cacheserver",
						Image: "placeholder:latest",
					}},
				},
			},
		},
	}
}

// setCacheOwner patches `obj` in the cluster to be controller-owned by
// `cache`. Retries on conflict with a fresh Get/Update each iteration.
func setCacheOwner(ctx context.Context, obj client.Object, cache *storagev1.Cache) {
	Eventually(func(g Gomega) {
		key := client.ObjectKeyFromObject(obj)
		g.Expect(k8sClient.Get(ctx, key, obj)).To(Succeed(), "fetching %T %s", obj, key)
		g.Expect(controllerutil.SetControllerReference(cache, obj, k8sClient.Scheme())).To(Succeed(), "setting controller ref on %T %s", obj, key)
		g.Expect(k8sClient.Update(ctx, obj)).To(Succeed(), "updating owner ref on %T %s", obj, key)
	}).Should(Succeed(), "setting Cache %s as owner of %T", cache.Name, obj)
}

// hasEventForCache asserts that an event with the given reason exists in
// `namespace` whose Regarding field references the given Cache by name.
func hasEventForCache(ctx context.Context, g Gomega, namespace, cacheName, reason string) {
	events := &eventsv1.EventList{}
	g.Expect(k8sClient.List(ctx, events, client.InNamespace(namespace))).To(Succeed(), "listing events in %s", namespace)
	g.Expect(events.Items).To(ContainElement(SatisfyAll(
		HaveField("Reason", reason),
		HaveField("Regarding.Kind", "Cache"),
		HaveField("Regarding.Name", cacheName),
	)), "expected a %q event for Cache %s in namespace %s", reason, cacheName, namespace)
}

var _ = Describe("Cache controller legacy backwards compatibility", Ordered, func() {
	BeforeAll(func(ctx SpecContext) {
		ns := corev1.Namespace{ObjectMeta: metav1.ObjectMeta{Name: legacyTestNamespace}}
		Expect(client.IgnoreAlreadyExists(k8sClient.Create(ctx, &ns))).To(Succeed(), "creating legacy test namespace")
		DeferCleanup(func(ctx SpecContext) {
			Expect(client.IgnoreNotFound(k8sClient.Delete(ctx, &ns))).To(Succeed(), "cleaning up legacy test namespace")
		})
	})

	It("recreates a pre-existing StatefulSet that has the wrong PodManagementPolicy", func(ctx SpecContext) {
		cacheName := "cacheserver"

		By("pre-creating a StatefulSet with the correct name but OrderedReady PMP")
		bad := legacyMinimalSTS(cacheName, legacyTestNamespace)
		Expect(k8sClient.Create(ctx, bad)).To(Succeed(), "pre-creating legacy STS %s", cacheName)
		DeferCleanup(func(ctx SpecContext) {
			Expect(client.IgnoreNotFound(k8sClient.Delete(ctx, bad))).To(Succeed(), "cleaning up legacy STS %s", cacheName)
		})

		By("creating the Cache so the controller starts reconciling")
		cr := createTestCache(cacheName, legacyTestNamespace, 2, true)
		Expect(k8sClient.Create(ctx, cr)).To(Succeed(), "creating Cache %s", cacheName)
		DeferCleanup(func(ctx SpecContext) {
			Expect(client.IgnoreNotFound(k8sClient.Delete(ctx, cr))).To(Succeed(), "cleaning up Cache %s", cacheName)
		})

		By("setting the Cache as owner on the pre-existing STS")
		Eventually(func(g Gomega) {
			fresh := &storagev1.Cache{}
			g.Expect(k8sClient.Get(ctx, client.ObjectKeyFromObject(cr), fresh)).To(Succeed(), "fetching Cache for UID")
			g.Expect(fresh.UID).NotTo(BeEmpty(), "Cache %s should have a UID assigned", cacheName)
			cr.UID = fresh.UID
		}).Should(Succeed(), "waiting for Cache %s to be persisted with a UID", cacheName)
		setCacheOwner(ctx, &appsv1.StatefulSet{ObjectMeta: metav1.ObjectMeta{
			Name: cacheName, Namespace: legacyTestNamespace,
		}}, cr)

		By("expecting the controller to recreate the STS with Parallel PMP")
		Eventually(func(g Gomega) {
			var sts appsv1.StatefulSet
			err := k8sClient.Get(ctx, types.NamespacedName{Name: cacheName, Namespace: legacyTestNamespace}, &sts)
			g.Expect(err).To(Succeed(), "fetching StatefulSet %s", cacheName)
			g.Expect(sts.Spec.PodManagementPolicy).To(Equal(appsv1.ParallelPodManagement), "STS %s should be recreated with Parallel PMP", cacheName)
			hasEventForCache(ctx, g, legacyTestNamespace, cacheName, storagev1.EventReasonRecreated)
		}).Should(Succeed(), "waiting for legacy STS %s to be recreated with Parallel PMP and a Recreated event", cacheName)
	})

	// Proof for isImmutableFieldErr: this spec talks directly to the
	// envtest apiserver (no controller) to demonstrate that updating an
	// immutable StatefulSet field (PodManagementPolicy) yields an
	// apierrors.IsInvalid error whose Status.Details.Causes contains a
	// CauseTypeForbidden entry. That is exactly the shape isImmutableFieldErr
	// matches on, so the controller's recreate path is keyed off real
	// apiserver behavior rather than an assumption.
	It("apiserver returns Invalid+FieldValueForbidden when patching an immutable STS field", func(ctx SpecContext) {
		stsName := "immutable-proof"

		By("creating a baseline STS with OrderedReady PMP")
		sts := legacyMinimalSTS(stsName, legacyTestNamespace)
		Expect(k8sClient.Create(ctx, sts)).To(Succeed(), "creating baseline STS %s", stsName)
		DeferCleanup(func(ctx SpecContext) {
			Expect(client.IgnoreNotFound(k8sClient.Delete(ctx, sts))).To(Succeed(), "cleaning up STS %s", stsName)
		})

		By("attempting to flip PodManagementPolicy to Parallel (immutable field)")
		sts.Spec.PodManagementPolicy = appsv1.ParallelPodManagement
		err := k8sClient.Update(ctx, sts)
		Expect(err).To(HaveOccurred(), "updating immutable PodManagementPolicy must fail")

		By("asserting the error is Invalid with a FieldValueForbidden cause")
		Expect(apierrors.IsInvalid(err)).To(BeTrue(), "expected apierrors.IsInvalid; got %v", err)

		var statusErr *apierrors.StatusError
		Expect(errors.As(err, &statusErr)).To(BeTrue(), "expected *apierrors.StatusError; got %T", err)
		Expect(statusErr.ErrStatus.Details).NotTo(BeNil(), "expected Status.Details on Invalid error: %+v", statusErr.ErrStatus)
		Expect(statusErr.ErrStatus.Details.Causes).To(ContainElement(HaveField("Type", metav1.CauseTypeForbidden)),
			"expected at least one CauseTypeForbidden cause; got %+v", statusErr.ErrStatus.Details.Causes)

		By("verifying isImmutableFieldErr matches this exact error")
		Expect(isImmutableFieldErr(err)).To(BeTrue(), "isImmutableFieldErr should match a real apiserver immutable-field error")
	})
})
