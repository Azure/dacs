// Package e2e contains end-to-end tests for the DACS cache system.
//
// This file contains controller-based tests that verify the Cache controller
// creates and manages resources correctly through the Cache CR lifecycle.
//
// # Running Tests
//
// Controller tests:
//
//	make test-e2e-controller
//
// # Environment Variables
//
//	INSTALL_CONTROLLER - Deploy using Cache controller instead of Helm
//	INSTALL_PREREQ     - Deploy dacs-prereq DaemonSet
//	UNINSTALL          - Clean up resources after tests
//	SKIP_SUPPORT_BUNDLE - Skip collecting diagnostics after tests
//
// # Controller Test Resources
//
// Controller tests create:
//   - Cache CR (cache-sample) in test namespace
//   - StatefulSet, Services, ConfigMaps (owned by Cache CR)
//   - Controller deployment in cache-controller-system namespace
//
// Tests are ordered and share a single Cache CR deployment for efficiency.
package e2e

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"time"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	appsv1 "k8s.io/api/apps/v1"
	batchv1 "k8s.io/api/batch/v1"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/types"
	"sigs.k8s.io/controller-runtime/pkg/client"

	"github.com/Azure/dacs/test/e2e/testutil"
	"github.com/Azure/dacs/test/pkg/perfclient"
)

// Controller test constants
const (
	// Cache CR resource names
	CacheCRName = "cache-sample"

	// StatefulSet and Service names
	CacheStatefulSetName = "cacheserver"
	CacheServiceName     = "cacheserver"
	DiscoveryServiceName = "cacheserver-discovery"

	// ConfigMap names
	DiscoveryConfigMapName = "cacheserver-discovery-config"

	// ConfigMap data keys
	ServerCountKey = "cache_server_count"

	// Labels
	CacheServerLabelKey   = "app"
	CacheServerLabelValue = "cacheserver"

	// Expected replica count from storage_v1_cache.yaml
	ExpectedReplicas = 2

	// Controller namespace
	ControllerNamespace = "cache-controller-system"

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
	// Skip controller tests when not running in controller mode
	BeforeAll(func() {
		if os.Getenv("INSTALL_CONTROLLER") != "true" {
			Skip("Skipping controller tests - INSTALL_CONTROLLER is not set to true")
		}
	})

	// Set default timeouts for controller tests
	SetDefaultEventuallyTimeout(5 * time.Minute)
	SetDefaultEventuallyPollingInterval(5 * time.Second)
	EnforceDefaultTimeoutsWhenUsingContexts()

	// Note: Controller deployment is handled by common.Setup() in BeforeSuite
	// when INSTALL_CONTROLLER=true is set. This test assumes controller and
	// Cache CR are already deployed.

	AfterAll(func(ctx SpecContext) {
		By("cleaning up perf test jobs")
		cmd := exec.CommandContext(ctx, "kubectl", "-n", namespace,
			"delete", "job", "--all", "--wait=true", "--timeout="+JobCleanupTimeout)
		if output, err := cmd.CombinedOutput(); err != nil {
			GinkgoWriter.Printf("Warning: job cleanup had issues: %v\nOutput: %s\n", err, string(output))
		}

		By("ensuring Cache CR is cleaned up")
		// Delete Cache CR to trigger cleanup of owned resources
		cmd = exec.CommandContext(ctx, "kubectl", "-n", namespace,
			"delete", "cache", CacheCRName, "--ignore-not-found=true", "--wait=true", "--timeout="+CacheDeleteTimeout)
		if output, err := cmd.CombinedOutput(); err != nil {
			GinkgoWriter.Printf("Warning: Cache CR cleanup had issues: %v\nOutput: %s\n", err, string(output))
		} else {
			GinkgoWriter.Println("Cache CR and owned resources cleaned up successfully")
		}
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
				var readyCondition *metav1.Condition
				for i := range cache.Status.Conditions {
					if cache.Status.Conditions[i].Type == "Ready" {
						readyCondition = &cache.Status.Conditions[i]
						break
					}
				}
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
					client.MatchingLabels{CacheServerLabelKey: CacheServerLabelValue})
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

		It("should run upload test successfully", func(ctx SpecContext) {
			By("creating and running upload perf test")
			serverHostname := testutil.BuildCacheServerHostnames(CacheStatefulSetName, CacheServiceName, namespace, ExpectedReplicas)
			jobConfig := perfclient.NewConfig().
				WithMode(perfclient.ModeUpload).
				WithNamespace(namespace).
				WithServerHostname(serverHostname).
				WithDataSizeGiB(1).
				WithNumThreads(1).
				WithNumIterations(1)

			job, err := jobConfig.LoadJob()
			Expect(err).NotTo(HaveOccurred())

			By("creating upload job")
			err = k8sClient.Create(ctx, job)
			Expect(err).NotTo(HaveOccurred())

			By("waiting for upload job to complete")
			Eventually(func(g Gomega) {
				updatedJob := &batchv1.Job{}
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      job.Name,
					Namespace: namespace,
				}, updatedJob)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(updatedJob.Status.Succeeded).To(BeNumerically(">", 0))
			}).WithContext(ctx).WithTimeout(3 * time.Minute).Should(Succeed())

			By("verifying upload job logs show success")
			logs := testutil.GetJobLogs(ctx, job.Name, namespace)
			Expect(logs).To(ContainSubstring("All iterations succeeded: true"))

			By("printing upload client logs")
			testutil.PrintClientLogs(ctx, namespace, job.Name)
		})

		It("should run download test successfully", func(ctx SpecContext) {
			By("creating and running download perf test")
			serverHostname := testutil.BuildCacheServerHostnames(CacheStatefulSetName, CacheServiceName, namespace, ExpectedReplicas)
			jobConfig := perfclient.NewConfig().
				WithMode(perfclient.ModeDownload).
				WithNamespace(namespace).
				WithServerHostname(serverHostname).
				WithDataSizeGiB(1).
				WithNumThreads(1).
				WithNumIterations(1)

			job, err := jobConfig.LoadJob()
			Expect(err).NotTo(HaveOccurred())

			By("creating download job")
			err = k8sClient.Create(ctx, job)
			Expect(err).NotTo(HaveOccurred())

			By("waiting for download job to complete")
			Eventually(func(g Gomega) {
				updatedJob := &batchv1.Job{}
				err := k8sClient.Get(ctx, types.NamespacedName{
					Name:      job.Name,
					Namespace: namespace,
				}, updatedJob)
				g.Expect(err).NotTo(HaveOccurred())
				g.Expect(updatedJob.Status.Succeeded).To(BeNumerically(">", 0))
			}).WithContext(ctx).WithTimeout(3 * time.Minute).Should(Succeed())

			By("verifying download job logs show success")
			logs := testutil.GetJobLogs(ctx, job.Name, namespace)
			Expect(logs).To(ContainSubstring("All iterations succeeded: true"))

			By("printing download client logs")
			testutil.PrintClientLogs(ctx, namespace, job.Name)
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
})
