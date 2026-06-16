package aks

import (
	"context"
	"fmt"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	eventsv1 "k8s.io/api/events/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"sigs.k8s.io/controller-runtime/pkg/client"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	akshelpers "github.com/Azure/dacs/test/pkg/aks"
	"github.com/Azure/dacs/test/pkg/kusto"
	"github.com/Azure/dacs/test/pkg/verification"
)

const (
	// Target node count for the scale-down phase. Must be less than the initial node count.
	scaleDownTargetNodes = 1

	// How long to wait for the K8s node list to reflect a nodepool scale.
	// 20m accommodates AKS nodepool scale latency.
	nodeCountSettleTimeout = 20 * time.Minute

	// How long to wait for cache-controller to update sts.spec.replicas.
	// 25m gives a cushion past its ~15m stabilization window.
	stsSpecSettleTimeout = 25 * time.Minute

	// How long to wait for sts status to converge to spec.replicas.
	// 15m covers image pulls + pod startup on a fresh node.
	stsStatusSettleTimeout = 15 * time.Minute

	// opScaleDown and opScaleUp are the operation names used when emitting
	// per-operation perf metrics for this spec.
	opScaleDown = "scale-down"
	opScaleUp   = "scale-up"
)

func testClusterScale(tc *testContext) {
	It("should survive scaling node pool down to 1 and back up to original count", Label("aks-scale"), func(ctx SpecContext) {
		// Scale back up to the original size rather than a hardcoded number.
		originalNodeCount := len(tc.initialNodeNames)
		Expect(originalNodeCount).To(BeNumerically(">", scaleDownTargetNodes),
			"initial node count (%d) must be greater than scale-down target (%d) for the test to exercise both directions",
			originalNodeCount, scaleDownTargetNodes)

		// Used to filter Scaling events to those emitted during the test window.
		scaleStart := metav1.NewMicroTime(time.Now())

		By("discovering default node pool")
		nodepool, err := akshelpers.GetDefaultNodePool(ctx, tc.subscription, tc.resourceGroupName, tc.managedCluster)
		Expect(err).NotTo(HaveOccurred(), "failed to discover default node pool")
		Expect(nodepool).NotTo(BeEmpty(), "default node pool name is empty")
		log.Info("discovered default node pool", "nodepool", nodepool, "originalNodeCount", originalNodeCount)

		By(fmt.Sprintf("scaling node pool %s down to %d node(s)", nodepool, scaleDownTargetNodes))
		scaleDownStart := time.Now()
		Expect(akshelpers.ScaleNodePool(ctx, tc.subscription, tc.resourceGroupName, tc.managedCluster, nodepool, scaleDownTargetNodes, log)).
			To(Succeed(), "failed to scale node pool down to %d", scaleDownTargetNodes)
		log.Info("az aks nodepool scale call returned",
			"direction", "down", "targetNodes", scaleDownTargetNodes,
			"elapsed", time.Since(scaleDownStart).Round(time.Second).String())

		By(fmt.Sprintf("waiting for node count to reach %d", scaleDownTargetNodes))
		waitForNodeCount(ctx, tc, scaleDownTargetNodes)
		downNodeReachedAt := time.Now()
		log.Info("node count reached target",
			"direction", "down", "targetNodes", scaleDownTargetNodes,
			"elapsedSinceScaleCall", time.Since(scaleDownStart).Round(time.Second).String())

		By(fmt.Sprintf("waiting for cacheserver statefulset to converge to %d replicas", scaleDownTargetNodes))
		waitForCacheserverReplicas(ctx, tc, scaleDownTargetNodes, downNodeReachedAt)
		scaleDownDuration := time.Since(scaleDownStart)
		log.Info("scale-down completed",
			"targetNodes", scaleDownTargetNodes,
			"scaleDownDuration", scaleDownDuration.Seconds())
		emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: opScaleDown, Details: kusto.ScaleOperationDetails{TargetNodes: scaleDownTargetNodes}}, scaleDownDuration)

		By(fmt.Sprintf("asserting cache-controller emitted a Scaling event reflecting %d replicas", scaleDownTargetNodes))
		assertScalingEvent(ctx, tc, scaleStart, scaleDownTargetNodes)

		By("verifying application health")
		Eventually(func(g Gomega, ctx context.Context) {
			verification.VerifyAppHealth(g, ctx, tc.Client, tc.namespace, log)
		}).WithContext(ctx).WithTimeout(10*time.Minute).WithPolling(10*time.Second).Should(Succeed(), "application health verification failed")

		By("verifying cache serving (upload+download round-trip) after scale-down")
		Expect(verification.VerifyCacheServing(ctx, tc.Client, tc.namespace, log)).
			To(Succeed(), "cache serving verification failed after scale-down to %d node(s)", scaleDownTargetNodes)

		By(fmt.Sprintf("scaling node pool %s up to %d nodes (original count)", nodepool, originalNodeCount))
		scaleUpStart := time.Now()
		Expect(akshelpers.ScaleNodePool(ctx, tc.subscription, tc.resourceGroupName, tc.managedCluster, nodepool, originalNodeCount, log)).
			To(Succeed(), "failed to scale node pool up to %d", originalNodeCount)
		log.Info("az aks nodepool scale call returned",
			"direction", "up", "targetNodes", originalNodeCount,
			"elapsed", time.Since(scaleUpStart).Round(time.Second).String())

		By(fmt.Sprintf("waiting for node count to reach %d", originalNodeCount))
		waitForNodeCount(ctx, tc, originalNodeCount)
		upNodeReachedAt := time.Now()
		log.Info("node count reached target",
			"direction", "up", "targetNodes", originalNodeCount,
			"elapsedSinceScaleCall", time.Since(scaleUpStart).Round(time.Second).String())

		By(fmt.Sprintf("waiting for cacheserver statefulset to converge to %d replicas", originalNodeCount))
		waitForCacheserverReplicas(ctx, tc, originalNodeCount, upNodeReachedAt)
		scaleUpDuration := time.Since(scaleUpStart)
		log.Info("scale-up completed",
			"targetNodes", originalNodeCount,
			"scaleUpDuration", scaleUpDuration.Seconds())
		emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: opScaleUp, Details: kusto.ScaleOperationDetails{TargetNodes: originalNodeCount}}, scaleUpDuration)

		By(fmt.Sprintf("asserting cache-controller emitted a Scaling event reflecting %d replicas", originalNodeCount))
		assertScalingEvent(ctx, tc, scaleStart, originalNodeCount)
	})
}

// assertScalingEvent verifies the cache-controller emitted a Scaling event
// mentioning targetReplicas after `since`. Polls because event delivery is async.
func assertScalingEvent(ctx context.Context, tc *testContext, since metav1.MicroTime, targetReplicas int) {
	target := fmt.Sprintf("to %d replicas", targetReplicas)
	Eventually(func(g Gomega, ctx context.Context) {
		var events eventsv1.EventList
		g.Expect(tc.List(ctx, &events, client.InNamespace(tc.namespace))).To(Succeed(), "failed to list events in %s", tc.namespace)
		g.Expect(events.Items).To(ContainElement(SatisfyAll(
			HaveField("Reason", storagev1.EventReasonScaling),
			HaveField("Note", ContainSubstring(target)),
			WithTransform(func(e eventsv1.Event) time.Time { return e.EventTime.Time },
				And(Not(BeZero()), Not(BeTemporally("<", since.Time)))),
		)), "expected a Scaling event with Note containing %q after %s", target, since.Time.Format(time.RFC3339))
	}).WithContext(ctx).WithTimeout(2*time.Minute).WithPolling(5*time.Second).
		Should(Succeed(), "cache-controller did not emit a Scaling event for target %d replicas", targetReplicas)
}

// waitForNodeCount polls the K8s node list until it matches expected.
func waitForNodeCount(ctx context.Context, tc *testContext, expected int) {
	Eventually(func(g Gomega, ctx context.Context) {
		var nl corev1.NodeList
		g.Expect(tc.List(ctx, &nl)).To(Succeed(), "listing nodes")
		g.Expect(nl.Items).To(HaveLen(expected), "expected %d node(s), got %d", expected, len(nl.Items))
	}).WithContext(ctx).WithTimeout(nodeCountSettleTimeout).WithPolling(30*time.Second).
		Should(Succeed(), "node count did not settle to %d within %s", expected, nodeCountSettleTimeout)
}

// waitForCacheserverReplicas waits for the cacheserver statefulset to fully
// converge to `target` replicas (spec + status). nodeReachedAt is used to
// log the elapsed time from node-count match to full sts convergence.
func waitForCacheserverReplicas(ctx context.Context, tc *testContext, target int, nodeReachedAt time.Time) {
	target32 := int32(target)
	listOpts := []client.ListOption{
		client.InNamespace(tc.namespace),
		client.MatchingLabels{"app": "cacheserver"},
	}
	timeout := stsSpecSettleTimeout + stsStatusSettleTimeout

	Eventually(func(g Gomega, ctx context.Context) {
		var stsList appsv1.StatefulSetList
		g.Expect(tc.List(ctx, &stsList, listOpts...)).To(Succeed(), "listing cacheserver statefulsets")
		g.Expect(stsList.Items).To(HaveLen(1), "expected exactly one cacheserver statefulset, got %d", len(stsList.Items))
		sts := stsList.Items[0]
		g.Expect(sts.Spec.Replicas).NotTo(BeNil(), "statefulset %s spec.replicas is nil", sts.Name)
		g.Expect(*sts.Spec.Replicas).To(Equal(target32),
			"statefulset %s spec.replicas=%d, want %d", sts.Name, *sts.Spec.Replicas, target)
		g.Expect(sts.Status.ObservedGeneration).To(BeNumerically(">=", sts.Generation),
			"statefulset %s status.observedGeneration=%d < generation=%d",
			sts.Name, sts.Status.ObservedGeneration, sts.Generation)
		g.Expect(sts.Status.Replicas).To(Equal(target32),
			"statefulset %s status.replicas=%d, want %d", sts.Name, sts.Status.Replicas, target)
		g.Expect(sts.Status.ReadyReplicas).To(Equal(target32),
			"statefulset %s status.readyReplicas=%d, want %d", sts.Name, sts.Status.ReadyReplicas, target)
		elapsed := time.Since(nodeReachedAt).Round(time.Second)
		log.Info("cacheserver statefulset converged",
			"name", sts.Name,
			"replicas", target,
			"generation", sts.Generation,
			"observedGeneration", sts.Status.ObservedGeneration,
			"elapsedSinceNodeCountReached", elapsed.String(),
		)
	}).WithContext(ctx).WithTimeout(timeout).WithPolling(30*time.Second).
		Should(Succeed(), "cacheserver statefulset did not converge to %d ready replicas within %s", target, timeout)
}
