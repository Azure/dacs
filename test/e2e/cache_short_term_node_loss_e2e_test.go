// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

// Package e2e contains end-to-end tests for the DACS cache system.
//
// This file verifies the cache scheduler's short-term node loss handling.
// When a node goes briefly unschedulable, the scheduler should hold the
// pod that was running on it in Pending state instead of rescheduling it to
// a different node. When the node comes back, the pod should bind back to
// it and the cached data should still be served.
//
// # Running Tests
//
//	make deploy CONTROLLER=new HELM_ARGS="--set cache.enabled=true"
//	make test-e2e TEST_FILTER=short-term-node-loss
//
// # Prerequisites
//
//   - Multi-node cluster with at least one cacheserver pod on a node that
//     is not hosting the cache-controller pod.
//   - dacs helm chart deployed (CONTROLLER=new, cache.enabled=true).
package e2e

import (
	"fmt"
	"os/exec"
	"time"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/meta"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/types"
	"sigs.k8s.io/controller-runtime/pkg/client"

	"github.com/Azure/dacs/test/pkg/verification"
)

var _ = Describe("Cache scheduler short-term node loss", Ordered,
	Label("e2e", "controller", "short-term-node-loss"), func() {

		// End-to-end check of the short-term node loss path. Drain a node and the
		// cacheserver pod it was hosting stays Pending instead of rescheduling it to a different node.
		// Once the node returns the pod binds back to it. The same data uploaded
		// before the drain must still be available afterwards.

		var (
			targetPod      string
			targetNode     string
			uploadedPrefix verification.DataPrefix
			nodeDrained    bool
			oldPodUID      types.UID
		)

		// Skip the suite if the Cache CRD is not installed (the dacs helm
		// chart has not been deployed).
		BeforeAll(func(ctx SpecContext) {
			cmd := exec.CommandContext(ctx, "kubectl", "get", "crd", "caches.storage.azure.com")
			if err := cmd.Run(); err != nil {
				Skip("Skipping short-term-node-loss tests - Cache CRD not installed (dacs helm chart not deployed)")
			}
		})

		BeforeAll(func(ctx SpecContext) {
			By("clearing any leftover snapshot interval override from a previous run")
			// Unset the CACHESERVERSNAPSHOTINTERVALSECONDS env var unconditionally so the cluster starts from a
			// clean state.
			cmd := exec.CommandContext(ctx, "kubectl", "set", "env",
				"-n", ControllerNamespace, "deployment/dacs-manager",
				"CACHE_CONTROLLER_CACHESERVERSNAPSHOTINTERVALSECONDS-")
			output, err := cmd.CombinedOutput()
			Expect(err).NotTo(HaveOccurred(), "kubectl set env (unset) failed: %s", string(output))

			By("waiting for the controller and cacheserver StatefulSet to settle on the default interval")
			cmd = exec.CommandContext(ctx, "kubectl", "rollout", "status",
				"-n", ControllerNamespace, "deployment/dacs-manager", "--timeout=3m")
			output, err = cmd.CombinedOutput()
			Expect(err).NotTo(HaveOccurred(), "controller rollout (reset) did not finish: %s", string(output))
			cmd = exec.CommandContext(ctx, "kubectl", "rollout", "status",
				"-n", namespace, "statefulset/"+CacheStatefulSetName, "--timeout=5m")
			output, err = cmd.CombinedOutput()
			Expect(err).NotTo(HaveOccurred(), "cacheserver rollout (reset) did not finish: %s", string(output))

			By("recording the cacheserver StatefulSet generation before setting snapshot interval")
			sts := &appsv1.StatefulSet{}
			Expect(k8sClient.Get(ctx, types.NamespacedName{Name: CacheStatefulSetName, Namespace: namespace}, sts)).To(Succeed())
			preRollGeneration := sts.Generation

			By("setting the cache server snapshot interval to 30s via the controller config")
			// Override the controller's CacheServerSnapshotIntervalSeconds env var so the cache server
			// flushes its snapshot to disk quickly enough for the upload below to survive the drain.
			// The controller restarts to pick up the new env, then re-renders the
			// cacheserver StatefulSet with the new CACHESERVER_SNAPSHOT_INTERVAL
			cmd = exec.CommandContext(ctx, "kubectl", "set", "env",
				"-n", ControllerNamespace, "deployment/dacs-manager",
				"CACHE_CONTROLLER_CACHESERVERSNAPSHOTINTERVALSECONDS=30")
			output, err = cmd.CombinedOutput()
			Expect(err).NotTo(HaveOccurred(), "kubectl set env failed: %s", string(output))

			By("waiting for the controller to restart with the new env")
			cmd = exec.CommandContext(ctx, "kubectl", "rollout", "status",
				"-n", ControllerNamespace, "deployment/dacs-manager", "--timeout=3m")
			output, err = cmd.CombinedOutput()
			Expect(err).NotTo(HaveOccurred(), "controller rollout did not finish: %s", string(output))

			By("waiting for the controller to update the cacheserver StatefulSet")
			Eventually(func(g Gomega) {
				latest := &appsv1.StatefulSet{}
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: CacheStatefulSetName, Namespace: namespace}, latest)).To(Succeed())
				g.Expect(latest.Generation).To(BeNumerically(">", preRollGeneration), "controller has not updated the StatefulSet yet")
				g.Expect(latest.Status.ObservedGeneration).To(Equal(latest.Generation), "StatefulSet controller has not observed the latest spec")
				g.Expect(latest.Status.CurrentRevision).To(Equal(latest.Status.UpdateRevision), "rolling update still in progress")
				g.Expect(latest.Status.ReadyReplicas).To(Equal(int32(ExpectedReplicas)), "not all replicas ready")
			}).WithContext(ctx).WithTimeout(5 * time.Minute).WithPolling(5 * time.Second).Should(Succeed())

			By("identifying the cache-controller pod's node so we don't cordon it")
			var controllerPods corev1.PodList
			Expect(k8sClient.List(ctx, &controllerPods,
				client.MatchingLabels{"control-plane": "controller-manager"},
			)).To(Succeed())
			controllerNodes := map[string]bool{}
			for _, p := range controllerPods.Items {
				if p.Spec.NodeName != "" {
					controllerNodes[p.Spec.NodeName] = true
				}
			}

			By("choosing a cacheserver pod whose node is not hosting the controller")
			var cacheserverPods corev1.PodList
			Expect(k8sClient.List(ctx, &cacheserverPods,
				client.InNamespace(namespace),
				client.MatchingLabels{
					CacheServerLabelKey: CacheServerLabelValue,
					CacheNameLabelKey:   CacheCRName,
				},
			)).To(Succeed())

			for _, p := range cacheserverPods.Items {
				if p.Spec.NodeName == "" || controllerNodes[p.Spec.NodeName] {
					continue
				}
				targetPod = p.Name
				targetNode = p.Spec.NodeName
				break
			}
			if targetPod == "" {
				Skip("no cacheserver pod found on a node that isn't hosting the controller; multi-node cluster required")
			}
			GinkgoWriter.Printf("Selected pod %s on node %s for short-term loss test\n", targetPod, targetNode)
		})

		// Always uncordon the node and reset the snapshot interval override,
		// even if the test failed mid-way, so the cluster is left in a clean
		// re-runnable state.
		AfterAll(func(ctx SpecContext) {
			if nodeDrained {
				By(fmt.Sprintf("uncordoning %s during cleanup", targetNode))
				cmd := exec.CommandContext(ctx, "kubectl", "uncordon", targetNode)
				if output, err := cmd.CombinedOutput(); err != nil {
					GinkgoWriter.Printf("Warning: uncordon cleanup failed for %s: %v\nOutput: %s\n",
						targetNode, err, string(output))
				}
				nodeDrained = false
			}

			By("unsetting the snapshot interval override on the controller deployment")
			cmd := exec.CommandContext(ctx, "kubectl", "set", "env",
				"-n", ControllerNamespace, "deployment/dacs-manager",
				"CACHE_CONTROLLER_CACHESERVERSNAPSHOTINTERVALSECONDS-")
			if output, err := cmd.CombinedOutput(); err != nil {
				GinkgoWriter.Printf("Warning: env unset cleanup failed: %v\nOutput: %s\n",
					err, string(output))
			}
		})

		It("populates the cache and confirms the baseline download", func(ctx SpecContext) {
			By("uploading test data to the cache")
			prefix, err := verification.UploadTestData(ctx, k8sClient, namespace, GinkgoLogr)
			Expect(err).NotTo(HaveOccurred(), "failed to upload baseline test data")
			uploadedPrefix = prefix

			By("downloading the data to confirm the cache is serving correctly")
			Expect(verification.DownloadAndVerify(ctx, k8sClient, namespace, uploadedPrefix, GinkgoLogr)).To(Succeed(),
				"baseline download failed")

			By("waiting for the cacheserver to take a snapshot of the uploaded data")
			// Wait slightly longer than the snapshot interval (30s) so the chunks
			// we just uploaded are persisted to the snapshot. Without this, the
			// cacheserver would wipe them on restart since they wouldn't be
			// referenced by any snapshot yet.
			time.Sleep(35 * time.Second)
		})

		It("holds the pod Pending and reports PodsWaitingForNodeRecovery while the node is cordoned", func(ctx SpecContext) {
			By(fmt.Sprintf("recording UID of pod %s before draining its node", targetPod))
			oldPod := &corev1.Pod{}
			Expect(k8sClient.Get(ctx, types.NamespacedName{Name: targetPod, Namespace: namespace}, oldPod)).To(Succeed())
			oldPodUID = oldPod.UID

			By(fmt.Sprintf("draining node %s so the cacheserver pod is gracefully evicted", targetNode))
			// drain cordons the node and then evicts pods
			cmd := exec.CommandContext(ctx, "kubectl", "drain", targetNode,
				"--ignore-daemonsets",
				"--delete-emptydir-data",
				"--force",
				"--timeout=2m")
			output, err := cmd.CombinedOutput()
			Expect(err).NotTo(HaveOccurred(), "kubectl drain failed: %s", string(output))
			nodeDrained = true

			By("waiting for the StatefulSet to recreate the pod in Pending state")
			Eventually(func(g Gomega) {
				p := &corev1.Pod{}
				err := k8sClient.Get(ctx, types.NamespacedName{Name: targetPod, Namespace: namespace}, p)
				g.Expect(err).NotTo(HaveOccurred(), "pod %s not yet recreated", targetPod)
				g.Expect(p.UID).NotTo(Equal(oldPodUID),
					"pod %s still has the original UID; eviction may not have completed", targetPod)
				g.Expect(p.Spec.NodeName).To(BeEmpty(),
					"pod %s already bound to %s; the scheduler should hold it Pending while node %s is cordoned",
					targetPod, p.Spec.NodeName, targetNode)
			}).WithContext(ctx).WithTimeout(2 * time.Minute).WithPolling(2 * time.Second).Should(Succeed())

			By("checking that the pod stays Pending while the node remains cordoned")
			Consistently(func(g Gomega) {
				p := &corev1.Pod{}
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: targetPod, Namespace: namespace}, p)).To(Succeed())
				g.Expect(p.Spec.NodeName).To(BeEmpty(),
					"pod %s was bound to %s while node %s is still cordoned; the scheduler reshuffled instead of waiting",
					targetPod, p.Spec.NodeName, targetNode)
			}).WithContext(ctx).WithTimeout(30 * time.Second).WithPolling(5 * time.Second).Should(Succeed())

			By("verifying that the Cache CR reports PodsWaitingForNodeRecovery on PodsScheduled")
			Eventually(func(g Gomega) {
				cache := &storagev1.Cache{}
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{
					Name:      CacheCRName,
					Namespace: namespace,
				}, cache)).To(Succeed())
				cond := meta.FindStatusCondition(cache.Status.Conditions, storagev1.PodsScheduledCondition)
				g.Expect(cond).NotTo(BeNil(), "PodsScheduled condition not present on Cache %s", CacheCRName)
				g.Expect(cond.Status).To(Equal(metav1.ConditionFalse),
					"expected PodsScheduled=False while pod %s is waiting; got status %q, reason %q, message %q",
					targetPod, cond.Status, cond.Reason, cond.Message)
				g.Expect(cond.Reason).To(Equal(storagev1.ReasonPodsWaitingForNodeRecovery),
					"expected reason %q; got %q (message: %q)",
					storagev1.ReasonPodsWaitingForNodeRecovery, cond.Reason, cond.Message)
			}).WithContext(ctx).WithTimeout(2 * time.Minute).WithPolling(5 * time.Second).Should(Succeed())
		})

		It("rebinds the pod to its original node once the cordon is removed and the cached data is still available", func(ctx SpecContext) {
			By(fmt.Sprintf("uncordoning %s", targetNode))
			cmd := exec.CommandContext(ctx, "kubectl", "uncordon", targetNode)
			output, err := cmd.CombinedOutput()
			Expect(err).NotTo(HaveOccurred(), "kubectl uncordon failed: %s", string(output))
			nodeDrained = false

			By(fmt.Sprintf("waiting for pod %s to be scheduled back onto %s", targetPod, targetNode))
			Eventually(func(g Gomega) {
				p := &corev1.Pod{}
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: targetPod, Namespace: namespace}, p)).To(Succeed())
				g.Expect(p.Spec.NodeName).To(Equal(targetNode),
					"pod %s was bound to %s; expected sticky rebind to %s",
					targetPod, p.Spec.NodeName, targetNode)
			}).WithContext(ctx).WithTimeout(3 * time.Minute).WithPolling(5 * time.Second).Should(Succeed())

			By(fmt.Sprintf("waiting for pod %s to be Running and Ready", targetPod))
			Eventually(func(g Gomega) {
				p := &corev1.Pod{}
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{Name: targetPod, Namespace: namespace}, p)).To(Succeed())
				g.Expect(p.Status.Phase).To(Equal(corev1.PodRunning))
				ready := false
				for _, c := range p.Status.Conditions {
					if c.Type == corev1.PodReady && c.Status == corev1.ConditionTrue {
						ready = true
						break
					}
				}
				g.Expect(ready).To(BeTrue(), "pod %s not yet Ready", targetPod)
			}).WithContext(ctx).WithTimeout(5 * time.Minute).WithPolling(5 * time.Second).Should(Succeed())

			By("waiting for the Cache CR to return to Ready")
			Eventually(func(g Gomega) {
				cache := &storagev1.Cache{}
				g.Expect(k8sClient.Get(ctx, types.NamespacedName{
					Name:      CacheCRName,
					Namespace: namespace,
				}, cache)).To(Succeed())
				g.Expect(meta.IsStatusConditionTrue(cache.Status.Conditions, storagev1.ReadyCondition)).To(BeTrue(),
					"Cache %s not yet Ready", CacheCRName)
			}).WithContext(ctx).WithTimeout(5 * time.Minute).WithPolling(5 * time.Second).Should(Succeed())

			By("verifying the previously uploaded data is still served by the cache")
			// Re-download the baseline data. Any chunks lost on the cordoned node would fail checksum verification.
			Expect(verification.DownloadAndVerify(ctx, k8sClient, namespace, uploadedPrefix, GinkgoLogr)).To(Succeed(),
				"data uploaded before the disruption no longer verifies; the cache on node %s did not survive the cordon/uncordon cycle",
				targetNode)
		})
	})
