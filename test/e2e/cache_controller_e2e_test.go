// Package e2e contains end-to-end tests for the DACS cache system.
//
// This file contains controller-based tests that verify the Cache controller
// creates and manages resources correctly through the Cache CR lifecycle.
//
// # Running Tests
//
// Controller tests:
//
//	make deploy CONTROLLER=new HELM_ARGS="--set cache.enabled=true"
//	make test-e2e TEST_FILTER=controller
//
// # Prerequisites
//
// Controller tests require:
//   - New controller deployed via helm (CONTROLLER=new)
//   - Cache CR deployed via helm (cache.enabled=true)
//
// # Controller Test Resources
//
// Tests verify controller-managed resources:
//   - Cache CR (cache-sample) in test namespace
//   - StatefulSet, Services, ConfigMaps (owned by Cache CR)
//   - Controller deployment in cache-server namespace
//
// Tests are ordered and share a single Cache CR deployment for efficiency.
package e2e

import (
	"context"
	"fmt"
	"os/exec"
	"time"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/api/meta"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/types"
	"k8s.io/utils/ptr"
	"sigs.k8s.io/controller-runtime/pkg/client"

	"github.com/Azure/dacs/test/e2e/testutil"
)

// Controller test constants
const (
	// Cache CR resource names
	CacheCRName = "cache-sample"

	// StatefulSet and Service names (derived from CacheCRName)
	CacheStatefulSetName = CacheCRName
	CacheServiceName     = CacheCRName
	DiscoveryServiceName = CacheCRName + storagev1.DiscoverySuffix

	// ConfigMap names
	DiscoveryConfigMapName = CacheCRName + storagev1.DiscoveryConfigSuffix

	// ConfigMap data keys
	ServerCountKey    = "cache_server_count"
	EndpointFormatKey = "cache_server_endpoint_format"

	// Labels
	CacheServerLabelKey   = "app"
	CacheServerLabelValue = "cacheserver"
	CacheNameLabelKey     = "dacs.azure.com/cache-name"

	// Expected replica count from storage_v1_cache.yaml
	ExpectedReplicas = 2

	// Multi-cache test constants
	MultiCacheName     = "cache-multi"
	MultiCacheReplicas = int32(1)

	RelabelCacheName = "cache-relabel"

	// Controller namespace
	ControllerNamespace = "cache-server"

	// Scaling test replica counts
	ScaleUpReplicas   = 3
	ScaleDownReplicas = 1

	// Environment variable names
	CacheServerCacheSizeBytesEnvVar = "CACHESERVER_CACHE_SIZE_BYTES"

	// Test timeouts
	JobCleanupTimeout  = "2m"
	CacheDeleteTimeout = "3m"
)

var _ = Describe("Cache Controller", Label("e2e", "controller"), Ordered, func() {
	// Skip controller tests if Cache CRD is not installed (controller not deployed)
	BeforeAll(func(ctx SpecContext) {
		cmd := exec.CommandContext(ctx, "kubectl", "get", "crd", "caches.storage.azure.com")
		if err := cmd.Run(); err != nil {
			Skip("Skipping controller tests - Cache CRD not installed (dacs helm chart not deployed)")
		}
	})

	// Set default timeouts for controller tests
	SetDefaultEventuallyTimeout(5 * time.Minute)
	SetDefaultEventuallyPollingInterval(5 * time.Second)
	EnforceDefaultTimeoutsWhenUsingContexts()

	// Note: Controller deployment is handled by helm deploy (CONTROLLER=new).
	// Cache CR is deployed via helm with cache.enabled=true.
	// Cleanup is handled by helm undeploy.
	// Note: perf tests run BEFORE controller tests to avoid DNS propagation
	// issues caused by StatefulSet mutations (scaling, spec updates).

	AfterAll(func(ctx SpecContext) {
		By("cleaning up test jobs created by controller tests")
		cmd := exec.CommandContext(ctx, "kubectl", "-n", namespace,
			"delete", "job", "--all", "--wait=true", "--timeout="+JobCleanupTimeout)
		if output, err := cmd.CombinedOutput(); err != nil {
			GinkgoWriter.Printf("Warning: job cleanup had issues: %v\nOutput: %s\n", err, string(output))
		}
		// Note: Cache CR cleanup is handled by helm undeploy
	})

	Describe("Cache resource lifecycle", func() {
		It("should have Cache CR reconciled successfully", func(ctx SpecContext) {
			By("verifying StatefulSet is created with correct replicas")
			var sts *appsv1.StatefulSet
			Eventually(func(g Gomega) {
				sts = &appsv1.StatefulSet{}
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      CacheStatefulSetName,
					Namespace: namespace,
				}, sts)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(sts.Status.ReadyReplicas).To(Equal(int32(ExpectedReplicas)))
				g.Expect(sts.Spec.Replicas).NotTo(BeNil(), "StatefulSet Spec.Replicas should not be nil")
				g.Expect(*sts.Spec.Replicas).To(Equal(int32(ExpectedReplicas)))
			}).WithContext(ctx).Should(Succeed())

			By("verifying StatefulSet has owner reference to Cache CR")
			Expect(sts.OwnerReferences).To(HaveLen(1))
			Expect(sts.OwnerReferences[0].Kind).To(Equal("Cache"))
			Expect(sts.OwnerReferences[0].Name).To(Equal(CacheCRName))
			Expect(sts.OwnerReferences[0].Controller).NotTo(BeNil(), "Controller field should not be nil")
			Expect(*sts.OwnerReferences[0].Controller).To(BeTrue())

			By("verifying StatefulSet pod template has correct labels")
			Expect(sts.Spec.Template.Labels[CacheServerLabelKey]).To(Equal(CacheServerLabelValue))
			Expect(sts.Spec.Template.Labels[CacheNameLabelKey]).To(Equal(CacheCRName))

			By("verifying StatefulSet selector includes cache-name label")
			Expect(sts.Spec.Selector.MatchLabels[CacheNameLabelKey]).To(Equal(CacheCRName))

			By("verifying headless Service is created with correct configuration")
			headlessSvc := &corev1.Service{}
			Eventually(func(g Gomega) {
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      CacheServiceName,
					Namespace: namespace,
				}, headlessSvc)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(headlessSvc.Spec.ClusterIP).To(Equal("None"))
				g.Expect(headlessSvc.Spec.Selector[CacheServerLabelKey]).To(Equal(CacheServerLabelValue))
				g.Expect(headlessSvc.Spec.Selector[CacheNameLabelKey]).To(Equal(CacheCRName))
				g.Expect(headlessSvc.Spec.Ports).NotTo(BeEmpty())
				g.Expect(headlessSvc.OwnerReferences).To(HaveLen(1))
				g.Expect(headlessSvc.OwnerReferences[0].Name).To(Equal(CacheCRName))
			}).WithContext(ctx).Should(Succeed())

			By("verifying discovery Service is created with correct configuration")
			discoverySvc := &corev1.Service{}
			Eventually(func(g Gomega) {
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      DiscoveryServiceName,
					Namespace: namespace,
				}, discoverySvc)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(discoverySvc.Spec.ClusterIP).NotTo(Equal("None"))
				g.Expect(discoverySvc.Spec.Selector[CacheServerLabelKey]).To(Equal(CacheServerLabelValue))
				g.Expect(discoverySvc.Spec.Selector[CacheNameLabelKey]).To(Equal(CacheCRName))
				g.Expect(discoverySvc.OwnerReferences).To(HaveLen(1))
				g.Expect(discoverySvc.OwnerReferences[0].Name).To(Equal(CacheCRName))
			}).WithContext(ctx).Should(Succeed())

			By("verifying discovery ConfigMap is created with correct data")
			cm := &corev1.ConfigMap{}
			Eventually(func(g Gomega) {
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      DiscoveryConfigMapName,
					Namespace: namespace,
				}, cm)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(cm.Data[ServerCountKey]).To(Equal(fmt.Sprintf("%d", ExpectedReplicas)))
				g.Expect(cm.OwnerReferences).To(HaveLen(1))
				g.Expect(cm.OwnerReferences[0].Name).To(Equal(CacheCRName))
			}).WithContext(ctx).Should(Succeed())

			By("verifying Cache CR status is Ready")
			Eventually(func(g Gomega) {
				cache := &storagev1.Cache{}
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      CacheCRName,
					Namespace: namespace,
				}, cache)
				g.Expect(err).NotTo(HaveOccurred())

				// Verify Ready condition
				readyCondition := meta.FindStatusCondition(cache.Status.Conditions, storagev1.ReadyCondition)
				g.Expect(readyCondition).NotTo(BeNil(), "Ready condition should exist")
				g.Expect(readyCondition.Status).To(Equal(metav1.ConditionTrue))
				g.Expect(cache.Status.CurrentServers).NotTo(BeNil(), "CurrentServers should not be nil")
				g.Expect(*cache.Status.CurrentServers).To(Equal(int32(ExpectedReplicas)))
			}).WithContext(ctx).Should(Succeed())

			By("verifying all cache server pods are running and ready")
			Eventually(func(g Gomega) {
				podList := &corev1.PodList{}
				err := k8sClient.List(ctx, podList,
					client.InNamespace(namespace),
					client.MatchingLabels{
						CacheServerLabelKey: CacheServerLabelValue,
						CacheNameLabelKey:   CacheCRName,
					})
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(podList.Items).To(HaveLen(ExpectedReplicas))

				for _, pod := range podList.Items {
					g.Expect(pod.Status.Phase).To(Equal(corev1.PodRunning))

					// Verify all containers are ready
					for _, containerStatus := range pod.Status.ContainerStatuses {
						g.Expect(containerStatus.Ready).To(BeTrue(),
							fmt.Sprintf("Container %s in pod %s should be ready", containerStatus.Name, pod.Name))
						g.Expect(containerStatus.RestartCount).To(BeNumerically("<=", 2),
							fmt.Sprintf("Container %s should not have excessive restarts", containerStatus.Name))
					}

					// Verify pod ready condition
					var readyCondition *corev1.PodCondition
					for i := range pod.Status.Conditions {
						if pod.Status.Conditions[i].Type == corev1.PodReady {
							readyCondition = &pod.Status.Conditions[i]
							break
						}
					}
					g.Expect(readyCondition).NotTo(BeNil())
					g.Expect(readyCondition.Status).To(Equal(corev1.ConditionTrue))
				}
			}).WithContext(ctx).Should(Succeed())

			By("printing Cache resource status and logs")
			testutil.PrintCacheStatus(ctx, CacheCRName, namespace)
			testutil.PrintControllerLogs(ctx, ControllerNamespace, 50)
			testutil.PrintServerLogs(ctx, namespace, 30)
		})

		It("should reconcile when scaling up replicas", func(ctx SpecContext) {
			By(fmt.Sprintf("scaling Cache to %d replicas", ScaleUpReplicas))
			testutil.ScaleCache(ctx, CacheCRName, namespace, ScaleUpReplicas)

			By(fmt.Sprintf("verifying StatefulSet scales to %d", ScaleUpReplicas))
			Eventually(func(g Gomega) {
				sts := &appsv1.StatefulSet{}
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      CacheStatefulSetName,
					Namespace: namespace,
				}, sts)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(sts.Spec.Replicas).NotTo(BeNil(), "StatefulSet Spec.Replicas should not be nil")
				g.Expect(*sts.Spec.Replicas).To(Equal(int32(ScaleUpReplicas)))
				g.Expect(sts.Status.ReadyReplicas).To(Equal(int32(ScaleUpReplicas)))
			}).WithContext(ctx).Should(Succeed())

			By(fmt.Sprintf("verifying Cache status reflects %d servers", ScaleUpReplicas))
			Eventually(func(g Gomega) {
				cache := &storagev1.Cache{}
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      CacheCRName,
					Namespace: namespace,
				}, cache)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(cache.Status.CurrentServers).NotTo(BeNil(), "CurrentServers should not be nil")
				g.Expect(*cache.Status.CurrentServers).To(Equal(int32(ScaleUpReplicas)))
			}).WithContext(ctx).Should(Succeed())

			By("verifying ConfigMap updated with new server count")
			cm := &corev1.ConfigMap{}
			Eventually(func(g Gomega) {
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      DiscoveryConfigMapName,
					Namespace: namespace,
				}, cm)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(cm.Data[ServerCountKey]).To(Equal(fmt.Sprintf("%d", ScaleUpReplicas)))
			}).WithContext(ctx).Should(Succeed())
		})

		It("should reconcile when scaling down replicas", func(ctx SpecContext) {
			By(fmt.Sprintf("scaling Cache to %d replica", ScaleDownReplicas))
			testutil.ScaleCache(ctx, CacheCRName, namespace, ScaleDownReplicas)

			By(fmt.Sprintf("verifying StatefulSet scales to %d", ScaleDownReplicas))
			Eventually(func(g Gomega) {
				sts := &appsv1.StatefulSet{}
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      CacheStatefulSetName,
					Namespace: namespace,
				}, sts)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(sts.Spec.Replicas).NotTo(BeNil(), "StatefulSet Spec.Replicas should not be nil")
				g.Expect(*sts.Spec.Replicas).To(Equal(int32(ScaleDownReplicas)))
				g.Expect(sts.Status.ReadyReplicas).To(Equal(int32(ScaleDownReplicas)))
			}).WithContext(ctx).Should(Succeed())

			By(fmt.Sprintf("verifying Cache status reflects %d server", ScaleDownReplicas))
			Eventually(func(g Gomega) {
				cache := &storagev1.Cache{}
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      CacheCRName,
					Namespace: namespace,
				}, cache)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(cache.Status.CurrentServers).NotTo(BeNil(), "CurrentServers should not be nil")
				g.Expect(*cache.Status.CurrentServers).To(Equal(int32(ScaleDownReplicas)))
			}).WithContext(ctx).Should(Succeed())

			By("verifying ConfigMap updated")
			cm := &corev1.ConfigMap{}
			Eventually(func(g Gomega) {
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      DiscoveryConfigMapName,
					Namespace: namespace,
				}, cm)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(cm.Data[ServerCountKey]).To(Equal(fmt.Sprintf("%d", ScaleDownReplicas)))
			}).WithContext(ctx).Should(Succeed())
		})

		It("should reconcile when scaling back to original replicas", func(ctx SpecContext) {
			By(fmt.Sprintf("scaling Cache back to %d replicas", ExpectedReplicas))
			testutil.ScaleCache(ctx, CacheCRName, namespace, ExpectedReplicas)

			By(fmt.Sprintf("verifying StatefulSet scales back to %d", ExpectedReplicas))
			Eventually(func(g Gomega) {
				sts := &appsv1.StatefulSet{}
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      CacheStatefulSetName,
					Namespace: namespace,
				}, sts)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(sts.Spec.Replicas).NotTo(BeNil(), "StatefulSet Spec.Replicas should not be nil")
				g.Expect(*sts.Spec.Replicas).To(Equal(int32(ExpectedReplicas)))
				g.Expect(sts.Status.ReadyReplicas).To(Equal(int32(ExpectedReplicas)))
			}).WithContext(ctx).Should(Succeed())
		})

		It("should reconcile when Cache spec is updated", func(ctx SpecContext) {
			By("updating Cache spec serverSizeInGB")
			Eventually(func(g Gomega, ctx context.Context) {
				cmd := exec.CommandContext(ctx, "kubectl", "-n", namespace,
					"patch", "cache", CacheCRName, "--type=json",
					"-p", `[{"op": "replace", "path": "/spec/serverSizeInGB", "value": 200}]`)
				output, err := cmd.CombinedOutput()
				g.Expect(err).NotTo(HaveOccurred(), "Failed to patch cache: %s", string(output))
			}).WithContext(ctx).Should(Succeed())

			By("verifying Cache CR is updated")
			Eventually(func(g Gomega) {
				cache := &storagev1.Cache{}
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      CacheCRName,
					Namespace: namespace,
				}, cache)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(cache.Spec.ServerSizeInGB).To(Equal(int32(200)))
			}).WithContext(ctx).Should(Succeed())

			By("verifying StatefulSet is reconciled with updated serverSizeInGB")
			Eventually(func(g Gomega) {
				sts := &appsv1.StatefulSet{}
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      CacheStatefulSetName,
					Namespace: namespace,
				}, sts)
				g.Expect(err).NotTo(HaveOccurred())
				// Verify CACHESERVER_CACHE_SIZE_BYTES env var reflects 200GB (200 * 1024^3 bytes)
				expectedCacheSizeBytes := fmt.Sprintf("%d", int64(200)*1024*1024*1024)
				containers := sts.Spec.Template.Spec.Containers
				g.Expect(containers).NotTo(BeEmpty())
				args := containers[0].Args
				g.Expect(args).NotTo(BeEmpty())
				g.Expect(args[0]).To(ContainSubstring(fmt.Sprintf("%s=%s", CacheServerCacheSizeBytesEnvVar, expectedCacheSizeBytes)))
			}).WithContext(ctx).Should(Succeed())

			By("restoring original serverSizeInGB")
			cmd := exec.CommandContext(ctx, "kubectl", "-n", namespace,
				"patch", "cache", CacheCRName, "--type=json",
				"-p", `[{"op": "replace", "path": "/spec/serverSizeInGB", "value": 100}]`)
			output, err := cmd.CombinedOutput()
			Expect(err).NotTo(HaveOccurred(), "Failed to restore serverSizeInGB: %s", string(output))
		})
	})

	Describe("Multi-cache support", Ordered, func() {
		// Create a second Cache CR in the same namespace to verify resource isolation.
		// The Ephemeral field is intentionally omitted; the webhook defaults it to
		// false (persistent storage). This ensures the custom scheduler is exercised
		// for this CR, since ephemeral caches use the default Kubernetes scheduler.
		BeforeAll(func(ctx SpecContext) {
			multiCache := &storagev1.Cache{
				ObjectMeta: metav1.ObjectMeta{
					Name:      MultiCacheName,
					Namespace: namespace,
				},
				Spec: storagev1.CacheSpec{
					ServerSizeInGB: 1,
					ServerThreads:  4,
					Scaling: storagev1.ScalingType{
						Static: &storagev1.StaticScaleConfig{
							NumServers: MultiCacheReplicas,
						},
					},
					NodeSelectorKey:   ptr.To("dacs.azure.com/processing-unit"),
					NodeSelectorValue: ptr.To("cpu"),
				},
			}
			Expect(k8sClient.Create(ctx, multiCache)).To(Succeed(), "Failed to create second Cache CR for multi-cache tests")

			DeferCleanup(func(ctx SpecContext) {
				key := types.NamespacedName{
					Name:      MultiCacheName,
					Namespace: namespace,
				}
				cache := &storagev1.Cache{
					ObjectMeta: metav1.ObjectMeta{
						Name:      key.Name,
						Namespace: key.Namespace,
					},
				}
				Expect(client.IgnoreNotFound(k8sClient.Delete(ctx, cache))).
					To(Succeed(), "deleting second Cache CR")

				Eventually(func(g Gomega) {
					var currentCache storagev1.Cache
					err := k8sClient.Get(ctx, key, &currentCache)
					g.Expect(client.IgnoreNotFound(err)).
						To(Succeed(), "getting second Cache CR during cleanup")
					g.Expect(errors.IsNotFound(err)).To(BeTrue(), "waiting for second Cache CR to be deleted")

					var sts appsv1.StatefulSet
					err = k8sClient.Get(ctx, key, &sts)
					g.Expect(client.IgnoreNotFound(err)).
						To(Succeed(), "getting second cache StatefulSet during cleanup")
					g.Expect(errors.IsNotFound(err)).To(BeTrue(), "waiting for second cache StatefulSet to be deleted")

					var pods corev1.PodList
					g.Expect(k8sClient.List(ctx, &pods,
						client.InNamespace(namespace),
						client.MatchingLabels{CacheNameLabelKey: MultiCacheName},
					)).To(Succeed(), "listing second cache pods during cleanup")
					g.Expect(pods.Items).To(BeEmpty(), "waiting for second cache pods to be deleted")
				}).WithContext(ctx).WithTimeout(2*time.Minute).
					Should(Succeed(), "deleting second cache and its child resources")
			})
		})

		It("should reach Ready state", func(ctx SpecContext) {
			Eventually(func(g Gomega) {
				cache := &storagev1.Cache{}
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      MultiCacheName,
					Namespace: namespace,
				}, cache)
				g.Expect(err).NotTo(HaveOccurred())

				readyCondition := meta.FindStatusCondition(cache.Status.Conditions, storagev1.ReadyCondition)
				g.Expect(readyCondition).NotTo(BeNil(), "Ready condition should exist")
				g.Expect(readyCondition.Status).To(Equal(metav1.ConditionTrue))
				g.Expect(cache.Status.CurrentServers).NotTo(BeNil())
				g.Expect(*cache.Status.CurrentServers).To(Equal(MultiCacheReplicas))
			}).WithContext(ctx).Should(Succeed())
		})

		It("should create resources with correct labels and selectors", func(ctx SpecContext) {
			multiStsName := MultiCacheName
			multiDiscoverySvcName := MultiCacheName + storagev1.DiscoverySuffix
			multiConfigMapName := MultiCacheName + storagev1.DiscoveryConfigSuffix

			By("verifying StatefulSet has correct dual-label selectors")
			sts := &appsv1.StatefulSet{}
			Eventually(func(g Gomega) {
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      multiStsName,
					Namespace: namespace,
				}, sts)
				g.Expect(err).NotTo(HaveOccurred())
			}).WithContext(ctx).Should(Succeed())

			Expect(sts.Spec.Template.Labels[CacheServerLabelKey]).To(Equal(CacheServerLabelValue))
			Expect(sts.Spec.Template.Labels[CacheNameLabelKey]).To(Equal(MultiCacheName))
			Expect(sts.Spec.Selector.MatchLabels[CacheServerLabelKey]).To(Equal(CacheServerLabelValue))
			Expect(sts.Spec.Selector.MatchLabels[CacheNameLabelKey]).To(Equal(MultiCacheName))

			By("verifying headless Service selector targets only the second cache")
			headlessSvc := &corev1.Service{}
			Eventually(func(g Gomega) {
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      MultiCacheName,
					Namespace: namespace,
				}, headlessSvc)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(headlessSvc.Spec.ClusterIP).To(Equal("None"))
				g.Expect(headlessSvc.Spec.Selector[CacheServerLabelKey]).To(Equal(CacheServerLabelValue))
				g.Expect(headlessSvc.Spec.Selector[CacheNameLabelKey]).To(Equal(MultiCacheName))
			}).WithContext(ctx).Should(Succeed())

			By("verifying discovery Service selector targets only the second cache")
			discoverySvc := &corev1.Service{}
			Eventually(func(g Gomega) {
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      multiDiscoverySvcName,
					Namespace: namespace,
				}, discoverySvc)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(discoverySvc.Spec.ClusterIP).NotTo(Equal("None"))
				g.Expect(discoverySvc.Spec.Selector[CacheServerLabelKey]).To(Equal(CacheServerLabelValue))
				g.Expect(discoverySvc.Spec.Selector[CacheNameLabelKey]).To(Equal(MultiCacheName))
			}).WithContext(ctx).Should(Succeed())

			By("verifying ConfigMap has correct server count and endpoint format")
			cm := &corev1.ConfigMap{}
			Eventually(func(g Gomega) {
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      multiConfigMapName,
					Namespace: namespace,
				}, cm)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(cm.Data[ServerCountKey]).To(Equal(fmt.Sprintf("%d", MultiCacheReplicas)))
			}).WithContext(ctx).Should(Succeed())

			expectedEndpointPrefix := fmt.Sprintf("%s-{}.%s.%s.svc.cluster.local",
				MultiCacheName, MultiCacheName, namespace)
			Expect(cm.Data).To(HaveKey(EndpointFormatKey))
			Expect(cm.Data[EndpointFormatKey]).To(Equal(expectedEndpointPrefix))
		})

		It("should isolate pods between caches via labels", func(ctx SpecContext) {
			By("verifying pods for the second cache have correct labels")
			multiPods := &corev1.PodList{}
			Eventually(func(g Gomega) {
				err := k8sClient.List(ctx, multiPods,
					client.InNamespace(namespace),
					client.MatchingLabels{
						CacheServerLabelKey: CacheServerLabelValue,
						CacheNameLabelKey:   MultiCacheName,
					})
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(multiPods.Items).To(HaveLen(int(MultiCacheReplicas)))
			}).WithContext(ctx).Should(Succeed())

			By("verifying pods for the first cache are still intact")
			firstPods := &corev1.PodList{}
			Eventually(func(g Gomega) {
				err := k8sClient.List(ctx, firstPods,
					client.InNamespace(namespace),
					client.MatchingLabels{
						CacheServerLabelKey: CacheServerLabelValue,
						CacheNameLabelKey:   CacheCRName,
					})
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(firstPods.Items).To(HaveLen(ExpectedReplicas))
			}).WithContext(ctx).Should(Succeed())

			By("verifying total pod count equals sum of both caches")
			allPods := &corev1.PodList{}
			err := k8sClient.List(ctx, allPods,
				client.InNamespace(namespace),
				client.MatchingLabels{CacheServerLabelKey: CacheServerLabelValue})
			Expect(err).NotTo(HaveOccurred())
			Expect(allPods.Items).To(HaveLen(ExpectedReplicas + int(MultiCacheReplicas)))
		})

		It("should place every cache pod on a unique node", func(ctx SpecContext) {
			By("collecting node assignments for all cache pods")
			var allPods corev1.PodList
			Eventually(func(g Gomega) {
				err := k8sClient.List(ctx, &allPods,
					client.InNamespace(namespace),
					client.MatchingLabels{
						CacheServerLabelKey: CacheServerLabelValue,
					})
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(allPods.Items).To(HaveLen(ExpectedReplicas + int(MultiCacheReplicas)))
				for _, pod := range allPods.Items {
					g.Expect(pod.Spec.NodeName).NotTo(BeEmpty(), "pod %s not yet scheduled", pod.Name)
				}
			}).WithContext(ctx).Should(Succeed())

			By("logging pod -> node mapping and verifying uniqueness")
			log := GinkgoLogr.WithName("node-placement")
			nodeToPod := map[string]string{}
			for _, pod := range allPods.Items {
				cacheName := pod.Labels[CacheNameLabelKey]
				log.Info("pod scheduled", "pod", pod.Name, "cache", cacheName, "node", pod.Spec.NodeName)

				if existing, ok := nodeToPod[pod.Spec.NodeName]; ok {
					Fail(fmt.Sprintf("node %s has multiple cache pods: %s and %s - custom scheduler should assign one pod per node",
						pod.Spec.NodeName, existing, pod.Name))
				}
				nodeToPod[pod.Spec.NodeName] = pod.Name
			}
		})

		It("should not affect first cache resources", func(ctx SpecContext) {
			By("verifying first cache StatefulSet still targets the correct cache")
			sts := &appsv1.StatefulSet{}
			err := k8sClient.Get(ctx, types.NamespacedName{
				Name:      CacheStatefulSetName,
				Namespace: namespace,
			}, sts)
			Expect(err).NotTo(HaveOccurred())
			Expect(sts.Spec.Selector.MatchLabels[CacheNameLabelKey]).To(Equal(CacheCRName))
			Expect(*sts.Spec.Replicas).To(Equal(int32(ExpectedReplicas)))

			By("verifying first cache headless Service still targets the correct cache")
			headlessSvc := &corev1.Service{}
			err = k8sClient.Get(ctx, types.NamespacedName{
				Name:      CacheServiceName,
				Namespace: namespace,
			}, headlessSvc)
			Expect(err).NotTo(HaveOccurred())
			Expect(headlessSvc.Spec.Selector[CacheNameLabelKey]).To(Equal(CacheCRName))

			By("verifying first cache discovery Service still targets the correct cache")
			discoverySvc := &corev1.Service{}
			err = k8sClient.Get(ctx, types.NamespacedName{
				Name:      DiscoveryServiceName,
				Namespace: namespace,
			}, discoverySvc)
			Expect(err).NotTo(HaveOccurred())
			Expect(discoverySvc.Spec.Selector[CacheNameLabelKey]).To(Equal(CacheCRName))
		})
	})
})

var _ = Describe("Cache Controller node relabeling", Label("e2e", "controller"), Serial, func() {
	It("should move a cache pod when its node label moves", func(ctx SpecContext) {
		const (
			cpuLabelKey       = "dacs.azure.com/processing-unit"
			cpuLabelValue     = "cpu"
			nodeSelectorKey   = "dacs.azure.com/relabel-test"
			nodeSelectorValue = "enabled"
		)

		var nodeList corev1.NodeList
		Expect(k8sClient.List(ctx, &nodeList,
			client.MatchingLabels{cpuLabelKey: cpuLabelValue})).To(Succeed())

		var cachePods corev1.PodList
		Expect(k8sClient.List(ctx, &cachePods,
			client.MatchingLabels{CacheServerLabelKey: CacheServerLabelValue})).To(Succeed())

		occupiedNodes := map[string]bool{}
		for _, pod := range cachePods.Items {
			occupiedNodes[pod.Spec.NodeName] = true
		}

		var freeNodes []string
		for _, node := range nodeList.Items {
			if occupiedNodes[node.Name] || node.Spec.Unschedulable {
				continue
			}
			for _, condition := range node.Status.Conditions {
				if condition.Type == corev1.NodeReady && condition.Status == corev1.ConditionTrue {
					freeNodes = append(freeNodes, node.Name)
					break
				}
			}
		}
		Expect(len(freeNodes)).To(BeNumerically(">=", 2), "requires two free, ready CPU cache nodes")
		sourceNode := freeNodes[0]
		destinationNode := freeNodes[1]

		DeferCleanup(func(ctx SpecContext) {
			for _, nodeName := range []string{sourceNode, destinationNode} {
				var node corev1.Node
				Expect(k8sClient.Get(ctx, types.NamespacedName{Name: nodeName}, &node)).To(Succeed())
				if _, found := node.Labels[nodeSelectorKey]; !found {
					continue
				}
				delete(node.Labels, nodeSelectorKey)
				Expect(k8sClient.Update(ctx, &node)).To(Succeed())
			}
		})

		By(fmt.Sprintf("adding the test label to source node %s", sourceNode))
		output, err := exec.CommandContext(
			ctx, "kubectl", "label", "node", sourceNode,
			nodeSelectorKey+"="+nodeSelectorValue, "--overwrite").CombinedOutput()
		Expect(err).NotTo(HaveOccurred(), "Failed to label source node: %s", string(output))

		cache := &storagev1.Cache{
			ObjectMeta: metav1.ObjectMeta{Name: RelabelCacheName, Namespace: namespace},
			Spec: storagev1.CacheSpec{
				ServerSizeInGB:    1,
				ServerThreads:     4,
				CpuLimit:          ptr.To[int32](1),
				MemoryLimitMB:     ptr.To[int32](1024),
				NodeSelectorKey:   ptr.To(nodeSelectorKey),
				NodeSelectorValue: ptr.To(nodeSelectorValue),
				Scaling: storagev1.ScalingType{
					Static: &storagev1.StaticScaleConfig{NumServers: 1},
				},
				Storage: &storagev1.StorageConfig{Ephemeral: ptr.To(false)},
			},
		}
		By("creating a cache that selects the test label")
		Expect(k8sClient.Create(ctx, cache)).To(Succeed())
		DeferCleanup(func(ctx SpecContext) {
			err := k8sClient.Delete(ctx, cache)
			Expect(err == nil || errors.IsNotFound(err)).To(BeTrue())
			Eventually(func() bool {
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name: RelabelCacheName, Namespace: namespace,
				}, &storagev1.Cache{})
				return errors.IsNotFound(err)
			}).WithContext(ctx).Should(BeTrue())
		})

		var originalUID types.UID
		Eventually(func(g Gomega) {
			var pods corev1.PodList
			g.Expect(k8sClient.List(ctx, &pods,
				client.InNamespace(namespace),
				client.MatchingLabels{CacheNameLabelKey: RelabelCacheName})).To(Succeed())
			g.Expect(pods.Items).To(HaveLen(1))
			g.Expect(pods.Items[0].Status.Phase).To(Equal(corev1.PodRunning))
			g.Expect(pods.Items[0].Spec.NodeName).To(Equal(sourceNode))
			originalUID = pods.Items[0].UID
		}).WithContext(ctx).Should(Succeed())

		By(fmt.Sprintf("moving the test label from %s to %s", sourceNode, destinationNode))
		output, err = exec.CommandContext(
			ctx, "kubectl", "label", "node", destinationNode,
			nodeSelectorKey+"="+nodeSelectorValue, "--overwrite").CombinedOutput()
		Expect(err).NotTo(HaveOccurred(), "Failed to label destination node: %s", string(output))
		output, err = exec.CommandContext(
			ctx, "kubectl", "label", "node", sourceNode,
			nodeSelectorKey+"-").CombinedOutput()
		Expect(err).NotTo(HaveOccurred(), "Failed to remove source node label: %s", string(output))

		By("waiting for the cache pod to be recreated on the destination node")
		Eventually(func(g Gomega) {
			var pods corev1.PodList
			g.Expect(k8sClient.List(ctx, &pods,
				client.InNamespace(namespace),
				client.MatchingLabels{CacheNameLabelKey: RelabelCacheName})).To(Succeed())
			g.Expect(pods.Items).To(HaveLen(1))
			g.Expect(pods.Items[0].UID).NotTo(Equal(originalUID))
			g.Expect(pods.Items[0].Spec.NodeName).To(Equal(destinationNode))
			g.Expect(pods.Items[0].Status.Phase).To(Equal(corev1.PodRunning))
		}).WithContext(ctx).Should(Succeed())
	})
})
