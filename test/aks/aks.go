package aks

import (
	"context"
	"fmt"
	"time"

	. "github.com/onsi/ginkgo/v2"
	"github.com/onsi/ginkgo/v2/types"
	. "github.com/onsi/gomega"

	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/util/wait"
	clientgoscheme "k8s.io/client-go/kubernetes/scheme"
	"sigs.k8s.io/controller-runtime/pkg/cache"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/client/config"
	ctrllog "sigs.k8s.io/controller-runtime/pkg/log"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	akshelpers "github.com/Azure/dacs/test/pkg/aks"
	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/diagnostics"
	cachemonitor "github.com/Azure/dacs/test/pkg/kube/cache"
	"github.com/Azure/dacs/test/pkg/kube/node"
	"github.com/Azure/dacs/test/pkg/kube/pod"
	"github.com/Azure/dacs/test/pkg/logger"
	"github.com/Azure/dacs/test/pkg/verification"
)

// log is the package logger; klog-style format written to GinkgoWriter.
var log = logger.New()

// testContext holds shared state for AKS resilience tests.
type testContext struct {
	client.Client
	runtimeCache      cache.Cache
	managedCluster    string
	resourceGroupName string
	subscription      string
	namespace         string
	initialNodeNames  []string
	initialReplicas   int32
	testStartTime     time.Time
}

var tc testContext

var _ = Describe("aks tests", Label("aks"), Serial, func() {
	BeforeEach(beforeEach)
	testClusterUpgrade(&tc)
	AfterEach(afterEach)
})

func beforeEach(ctx context.Context) {
	ctrllog.SetLogger(log)

	// Set first so diagnostic-link KQL windows cover the entire BeforeEach
	// (health checks, smoke test). If BeforeEach itself fails the links will
	// scope to where the activity actually happened.
	tc.testStartTime = time.Now()

	// Registered first so it runs last via DeferCleanup's LIFO ordering -
	// diagnostic links should be the final thing printed for any outcome
	// (pass, fail, or skip), making them easy to find in the pipeline log.
	DeferCleanup(generateLogLinks)

	tc.namespace = common.GetNamespace()

	By("creating kubernetes client")
	clientScheme := runtime.NewScheme()
	Expect(clientgoscheme.AddToScheme(clientScheme)).To(Succeed())
	Expect(storagev1.AddToScheme(clientScheme)).To(Succeed())

	cfg, err := config.GetConfig()
	Expect(err).NotTo(HaveOccurred(), "failed to get kubeconfig")

	tc.Client, err = client.New(cfg, client.Options{Scheme: clientScheme})
	Expect(err).NotTo(HaveOccurred(), "failed to create k8s client")

	By("discovering AKS cluster info from node metadata")
	var nodeList corev1.NodeList
	Eventually(func(g Gomega, ctx context.Context) {
		g.Expect(tc.List(ctx, &nodeList)).To(Succeed())
		g.Expect(nodeList.Items).NotTo(BeEmpty(), "no nodes found")
	}).WithContext(ctx).WithTimeout(2*time.Minute).WithPolling(5*time.Second).Should(Succeed(), "failed to list nodes")

	providerID := nodeList.Items[0].Spec.ProviderID
	Expect(providerID).NotTo(BeEmpty(), "node providerID is empty")

	nodeRG, err := akshelpers.GetNodeResourceGroup(providerID)
	Expect(err).NotTo(HaveOccurred(), "failed to parse node resource group")

	tc.subscription, err = akshelpers.GetSubscription(providerID)
	Expect(err).NotTo(HaveOccurred(), "failed to parse subscription")

	var clusterInfo akshelpers.ManagedClusterInfo
	Eventually(func(g Gomega, ctx context.Context) {
		var err error
		clusterInfo, err = akshelpers.GetManagedClusterInfo(ctx, tc.subscription, nodeRG, log)
		g.Expect(err).NotTo(HaveOccurred())
	}).WithContext(ctx).WithTimeout(2*time.Minute).WithPolling(5*time.Second).Should(Succeed(), "failed to get managed cluster info")
	tc.managedCluster = clusterInfo.Name
	tc.resourceGroupName = clusterInfo.ResourceGroup

	log.Info("discovered AKS cluster",
		"cluster", tc.managedCluster,
		"resourceGroup", tc.resourceGroupName,
		"subscription", tc.subscription,
		"namespace", tc.namespace,
	)

	By("starting controller-runtime cache and monitors")
	runtimeCache, err := cache.New(cfg, cache.Options{Scheme: clientScheme})
	Expect(err).NotTo(HaveOccurred(), "failed to create cache")
	tc.runtimeCache = runtimeCache

	// The monitors must outlive the BeforeEach ctx, which Ginkgo cancels as
	// soon as BeforeEach returns. Use a background-rooted context so the
	// informer keeps firing during the It body; DeferCleanup tears it down.
	monitorCtx, cancelMonitors := context.WithCancel(context.Background())
	errCh := make(chan error, 1)
	go func() {
		errCh <- tc.runtimeCache.Start(monitorCtx)
	}()

	// Register informers via AddMonitor before WaitForCacheSync so the wait
	// covers the informers we actually use.
	err = node.AddMonitor(monitorCtx, tc.Client, tc.runtimeCache, []string{tc.namespace}, log)
	Expect(err).NotTo(HaveOccurred(), "failed to add node monitor")

	restartTracker, err := pod.AddMonitor(monitorCtx, tc.runtimeCache, tc.namespace, log)
	Expect(err).NotTo(HaveOccurred(), "failed to add pod monitor")

	err = cachemonitor.AddMonitor(monitorCtx, tc.runtimeCache, tc.namespace, log)
	Expect(err).NotTo(HaveOccurred(), "failed to add cache CR monitor")

	Expect(tc.runtimeCache.WaitForCacheSync(ctx)).To(BeTrue(), "failed to sync cache")

	DeferCleanup(func(ctx SpecContext) {
		cancelMonitors()
		Expect(<-errCh).NotTo(HaveOccurred(), "cache stopped with error")
		// Pod restarts during AKS upgrade are expected (node drain).
		// Log them for diagnostics but don't fail the test - data integrity
		// is verified separately in afterEach.
		if restarts := restartTracker.Snapshot(); len(restarts) > 0 {
			log.Info("container restarts observed during test", "restarts", restarts)
		}
	})

	By("verifying infrastructure health before test")
	Eventually(func(g Gomega, ctx context.Context) {
		verification.VerifyInfraHealth(g, ctx, tc.Client, log)
	}).WithContext(ctx).WithTimeout(10*time.Minute).WithPolling(10*time.Second).Should(Succeed(), "infrastructure health verification failed before test")

	By("verifying application health before test")
	Eventually(func(g Gomega, ctx context.Context) {
		verification.VerifyAppHealth(g, ctx, tc.Client, tc.namespace, log)
	}).WithContext(ctx).WithTimeout(10*time.Minute).WithPolling(10*time.Second).Should(Succeed(), "application health verification failed before test")

	By("verifying cache serving (upload+download round-trip) before test")
	Expect(verification.VerifyCacheServing(ctx, tc.namespace, log)).
		To(Succeed(), "cache serving verification failed before test")

	By("recording initial cluster state")
	var sts appsv1.StatefulSet
	Eventually(func(g Gomega, ctx context.Context) {
		var stsList appsv1.StatefulSetList
		g.Expect(tc.List(ctx, &stsList,
			client.InNamespace(tc.namespace),
			client.MatchingLabels{"app": "cacheserver"},
		)).To(Succeed())
		g.Expect(stsList.Items).To(HaveLen(1), "expected exactly one cacheserver statefulset, got %d", len(stsList.Items))
		sts = stsList.Items[0]
	}).WithContext(ctx).WithTimeout(2*time.Minute).WithPolling(5*time.Second).Should(Succeed(), "failed to list cacheserver statefulsets")
	Expect(sts.Spec.Replicas).NotTo(BeNil(), "statefulset spec.replicas is nil")
	tc.initialReplicas = *sts.Spec.Replicas
	tc.initialNodeNames = make([]string, len(nodeList.Items))
	for i, n := range nodeList.Items {
		tc.initialNodeNames[i] = n.Name
	}
	log.Info("recorded initial state",
		"nodeCount", len(tc.initialNodeNames),
		"replicas", tc.initialReplicas,
	)
}

func afterEach(ctx context.Context) {

	report := CurrentSpecReport()
	if report.State == types.SpecStateSkipped {
		return
	}
	if report.Failed() {
		// Support bundle collected in ReportAfterSuite (aks_suite_test.go).
		return
	}

	By("comparing post-upgrade node set")
	var postNodes corev1.NodeList
	Eventually(func(g Gomega, ctx context.Context) {
		g.Expect(tc.List(ctx, &postNodes)).To(Succeed())
	}).WithContext(ctx).WithTimeout(2*time.Minute).WithPolling(5*time.Second).Should(Succeed(), "failed to list nodes after test")
	postNodeNames := make([]string, len(postNodes.Items))
	for i, n := range postNodes.Items {
		postNodeNames[i] = n.Name
	}
	log.Info("node set comparison",
		"preNodes", tc.initialNodeNames,
		"postNodes", postNodeNames,
	)

	By("verifying infrastructure health")
	Eventually(func(g Gomega, ctx context.Context) {
		verification.VerifyInfraHealth(g, ctx, tc.Client, log)
	}).WithContext(ctx).WithTimeout(10*time.Minute).WithPolling(10*time.Second).Should(Succeed(), "infrastructure health verification failed")

	By("verifying application health")
	Eventually(func(g Gomega, ctx context.Context) {
		verification.VerifyAppHealth(g, ctx, tc.Client, tc.namespace, log)
	}).WithContext(ctx).WithTimeout(10*time.Minute).WithPolling(10*time.Second).Should(Succeed(), "application health verification failed")

	By("verifying data integrity")
	Expect(verification.VerifyCacheServing(ctx, tc.namespace, log)).To(Succeed(), "cache serving verification failed")
}

// generateLogLinks discovers the Log Analytics workspace and writes an ADX
// deep-link summary file. Errors are logged and swallowed - link generation
// should never fail the test. Registered as DeferCleanup in beforeEach so it
// is the last thing printed regardless of test outcome.
func generateLogLinks(ctx SpecContext) {

	if tc.subscription == "" || tc.resourceGroupName == "" || tc.managedCluster == "" {
		// beforeEach failed before populating cluster info; nothing to link to.
		return
	}
	var ws diagnostics.WorkspaceInfo
	wsErr := wait.PollUntilContextTimeout(ctx, 5*time.Second, 2*time.Minute, true, func(ctx context.Context) (bool, error) {
		var err error
		ws, err = diagnostics.GetWorkspaceFromCluster(ctx, tc.subscription, tc.resourceGroupName, tc.managedCluster)
		if err != nil {
			log.Info("workspace lookup failed; retrying", "error", err)
			return false, nil
		}
		return true, nil
	})
	if wsErr != nil {
		log.Info("skipping ADX link generation - workspace not found", "error", wsErr)
		return
	}

	links := diagnostics.GenerateLogLinks(ws, tc.testStartTime, time.Now(), tc.namespace, tc.subscription, tc.resourceGroupName, tc.managedCluster)
	fmt.Printf(`
========================================
  Diagnostic Links
========================================
ASI:                %s
Cache Server Logs:  %s
Controller Logs:    %s
Kube Events:        %s
========================================
`, links.ASI, links.CacheServerLogs, links.ControllerLogs, links.KubeEvents)

	if err := diagnostics.WriteLinksSummary(links, "diagnostic-links.md"); err != nil {
		log.Info("failed to write diagnostic links summary", "error", err)
	}
}
