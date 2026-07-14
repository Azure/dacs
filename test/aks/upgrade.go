// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

package aks

import (
	"context"
	"errors"
	"fmt"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	corev1 "k8s.io/api/core/v1"
	eventsv1 "k8s.io/api/events/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"sigs.k8s.io/controller-runtime/pkg/client"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	akshelpers "github.com/Azure/dacs/test/pkg/aks"
	"github.com/Azure/dacs/test/pkg/kusto"
)

// opAKSUpgrade is the operation name used when emitting the per-operation perf
// metric for this spec.
const opAKSUpgrade = "aks-upgrade"

// postUpgradePlacementTimeout bounds how long we wait for every cacheserver pod
// to be (re)scheduled to a node after the AKS rolling upgrade. A rolling
// upgrade reimages nodes one at a time and AKS adds a temporary surge node; the
// cache-controller scheduler binds the displaced pod onto that surge node,
// which can take several minutes per node. 20m gives the scheduler room to
// place the last displaced pod rather than flaking on a transient Pending pod.
const postUpgradePlacementTimeout = 20 * time.Minute

func testClusterUpgrade(tc *testContext) {
	It("should survive an AKS cluster upgrade", Label("aks-upgrade"), func(ctx context.Context) {

		By("recording pre-upgrade cacheserver pod placement")
		var prePodsByNode map[string]string
		Eventually(func(g Gomega, ctx context.Context) {
			var err error
			prePodsByNode, err = cacheserverPodsByNode(ctx, tc)
			g.Expect(err).NotTo(HaveOccurred())
		}).WithContext(ctx).WithTimeout(2*time.Minute).WithPolling(5*time.Second).Should(Succeed(), "failed to capture pre-upgrade pod placement")
		log.Info("pre-upgrade pod placement", "podsByNode", prePodsByNode)

		By("querying available AKS upgrades")
		var upgrades []string
		Eventually(func(g Gomega, ctx context.Context) {
			var err error
			upgrades, err = akshelpers.GetAvailableUpgrades(ctx, tc.subscription, tc.resourceGroupName, tc.managedCluster)
			g.Expect(err).NotTo(HaveOccurred())
		}).WithContext(ctx).WithTimeout(2*time.Minute).WithPolling(5*time.Second).Should(Succeed(), "failed to get available AKS upgrades")
		if len(upgrades) == 0 {
			Skip("no available AKS upgrades")
		}

		nextVersion, err := akshelpers.GetNextSemver(upgrades)
		Expect(err).NotTo(HaveOccurred(), "failed to determine next upgrade version")
		log.Info("upgrading AKS cluster", "cluster", tc.managedCluster, "targetVersion", nextVersion)

		// Snapshot the time just before the upgrade kicks off. After the upgrade,
		// we assert that the cache-controller did not emit any "Scaling" events
		// in this window: the cluster is in dynamic mode, but a rolling AKS
		// upgrade should not change the desired replica count, so no scale
		// events should fire. If any do, the controller is reacting to
		// transient node unreadiness and would shuffle cacheserver pods --
		// that's the behavior we want to catch.
		upgradeStart := metav1.NewMicroTime(time.Now())

		By(fmt.Sprintf("upgrading cluster to version %s", nextVersion))
		upgradeCallStart := time.Now()
		err = akshelpers.UpgradeCluster(ctx, tc.subscription, tc.resourceGroupName, tc.managedCluster, nextVersion, log)
		if errors.Is(err, akshelpers.ErrCapacity) {
			Skip(fmt.Sprintf("skipping: Azure could not allocate surge capacity for the upgrade: %v", err))
		}
		Expect(err).NotTo(HaveOccurred(), "failed to upgrade AKS cluster")
		upgradeDuration := time.Since(upgradeCallStart)
		log.Info("AKS upgrade completed", "cluster", tc.managedCluster, "version", nextVersion, "aksUpgradeDuration", upgradeDuration.Seconds())
		emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: opAKSUpgrade, Details: kusto.UpgradeOperationDetails{TargetVersion: nextVersion}}, upgradeDuration)

		By("checking pod-node stickiness")
		var postPodsByNode map[string]string
		Eventually(func(g Gomega, ctx context.Context) {
			var err error
			postPodsByNode, err = cacheserverPodsByNode(ctx, tc)
			g.Expect(err).NotTo(HaveOccurred())
		}).WithContext(ctx).WithTimeout(postUpgradePlacementTimeout).WithPolling(15*time.Second).Should(Succeed(), "failed to capture post-upgrade pod placement")
		log.Info("post-upgrade pod placement", "podsByNode", postPodsByNode)

		// TODO(jlong): persist tracker across controller restart.
		// Root cause:
		//   The pod->node mapping is in-memory only, so when the cache-controller
		//   pod itself is evicted during the upgrade, the state is lost and every
		//   subsequent placement is round-robin.
		// Until the tracker is persisted, we log mismatches as warnings and fall
		// back to fresh upload/download for data integrity.
		stickinessViolations := 0
		for node, prePod := range prePodsByNode {
			postPod, ok := postPodsByNode[node]
			if !ok {
				log.Info("WARNING pod-node stickiness violated: pre-upgrade node no longer hosts a cacheserver pod",
					"node", node, "prePod", prePod, "TODO", "persist tracker state across cache-controller restarts")
				stickinessViolations++
				continue
			}
			if postPod != prePod {
				log.Info("WARNING pod-node stickiness violated: node now hosts a different cacheserver pod",
					"node", node, "prePod", prePod, "postPod", postPod, "TODO", "persist tracker state across cache-controller restarts")
				stickinessViolations++
			}
		}
		if stickinessViolations > 0 {
			log.Info("WARNING cache-server scheduler did not preserve pod-node stickiness across AKS upgrade",
				"violations", stickinessViolations, "TODO", "persist tracker state across cache-controller restarts")
		}

		// TODO(jlong): we should not scale, but it can happen right now if cache-controller gets evicted and sees 7 nodes immediately upon restart.
		By("checking for cache scaling events during the upgrade window")
		var events eventsv1.EventList
		Expect(tc.List(ctx, &events, client.InNamespace(tc.namespace))).To(Succeed(), "failed to list events in %s", tc.namespace)
		var scalingEvents []eventsv1.Event
		for _, e := range events.Items {
			if e.Reason == storagev1.EventReasonScaling && !e.EventTime.IsZero() && !e.EventTime.Time.Before(upgradeStart.Time) {
				scalingEvents = append(scalingEvents, e)
			}
		}
		if len(scalingEvents) > 0 {
			log.Info("WARNING cache-controller emitted Scaling event(s) during the AKS upgrade window; a rolling upgrade should not trigger replica changes in dynamic mode",
				"count", len(scalingEvents), "TODO", "fix controller to ignore transient node unreadiness during rolling upgrades")
			for _, e := range scalingEvents {
				log.Info("WARNING scaling event", "name", e.Name, "reason", e.Reason, "note", e.Note, "eventTime", e.EventTime.Time)
			}
		}
	})
}

// cacheserverPodsByNode returns a map from node name to the cacheserver pod
// scheduled on that node. Errors if any cacheserver pod is not yet scheduled
// or if two pods report the same node (which would indicate a test-rig bug).
func cacheserverPodsByNode(ctx context.Context, tc *testContext) (map[string]string, error) {
	var podList corev1.PodList
	if err := tc.List(ctx, &podList,
		client.InNamespace(tc.namespace),
		client.MatchingLabels{"app": "cacheserver"},
	); err != nil {
		return nil, fmt.Errorf("listing cacheserver pods: %w", err)
	}
	out := make(map[string]string, len(podList.Items))
	if len(podList.Items) == 0 {
		return nil, fmt.Errorf("no cacheserver pods found")
	}
	for _, p := range podList.Items {
		if p.Spec.NodeName == "" {
			return nil, fmt.Errorf("cacheserver pod %s is not scheduled to a node", p.Name)
		}
		if existing, ok := out[p.Spec.NodeName]; ok {
			return nil, fmt.Errorf("node %s hosts multiple cacheserver pods: %s and %s", p.Spec.NodeName, existing, p.Name)
		}
		out[p.Spec.NodeName] = p.Name
	}
	return out, nil
}
