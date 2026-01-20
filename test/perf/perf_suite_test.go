package perf

import (
	"context"
	"flag"
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	"github.com/google/uuid"
	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"k8s.io/client-go/tools/clientcmd"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/client/config"

	"github.com/Azure/dacs/test/pkg/cluster"
	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/git"
	"github.com/Azure/dacs/test/pkg/helm"
	"github.com/Azure/dacs/test/pkg/kusto"
	"github.com/Azure/dacs/test/pkg/logger"
	"github.com/Azure/dacs/test/pkg/utils"
)

var log = logger.New()

const namespace = "cache-server"

var (
	// suiteUUID is a unique identifier for this test run, generated at startup.
	suiteUUID = uuid.New().String()

	// k8sClient is the controller-runtime client for interacting with the cluster.
	k8sClient client.Client

	// defaultKubeConfigPath is the default path to the kubeconfig file.
	defaultKubeConfigPath = filepath.Join(os.Getenv("HOME"), ".kube", "config")
	// junitReport is the report file generated for consumption by Junit.
	junitReport = flag.String("junit-report", "junit.xml", "Path to the junit report")

	// supportBundleDir is the directory where support bundles are written.
	supportBundleDir = flag.String("support-bundle-dir", "support-bundles", "Path to write support-bundles")

	// summaryReport is the path to the summary report.
	summaryReport = flag.String("summary-report", "summary.md", "Path to write summary report")

	// Perf test configuration flags
	dataSizeGiB       = flag.Int("data-size-gib", 1, "Data size in GiB for perf tests")
	cacheServerCount  = flag.Int("cache-server-count", 1, "Number of cache servers to use")
	numThreads        = flag.Int("num-threads", 1, "Number of threads for perf client")
	checksumAlgorithm = flag.String("checksum-algorithm", "xxhash32", "Checksum algorithm (xxhash32 or crc32)")
)

func TestMain(m *testing.M) {
	if os.Getenv(clientcmd.RecommendedConfigPathEnvVar) == "" {
		err := os.Setenv(clientcmd.RecommendedConfigPathEnvVar, defaultKubeConfigPath)
		if err != nil {
			log.Error(err, "Failed to set default kubeconfig path")
			os.Exit(1)
		}
	}
	flag.Parse()

	// Set up controller-runtime client
	cfg, err := config.GetConfig()
	if err != nil {
		log.Error(err, "Failed to get kubeconfig")
		os.Exit(1)
	}

	k8sClient, err = client.New(cfg, client.Options{})
	if err != nil {
		log.Error(err, "Failed to create controller-runtime client")
		os.Exit(1)
	}

	os.Exit(m.Run())
}

// TestPerf runs the performance test suite for DACS cache server.
func TestPerf(t *testing.T) {
	RegisterFailHandler(Fail)
	log.Info("Starting DACS Performance test suite")
	RunSpecs(t, "perf suite")
}

var _ = BeforeSuite(func(ctx context.Context) {
	common.Setup(ctx, k8sClient, namespace)
})

var _ = AfterSuite(func(ctx context.Context) {
	cleanupPerfJobs(ctx, namespace)
	common.Teardown(ctx, k8sClient, namespace, *supportBundleDir)
})

var _ = ReportAfterSuite("perf reporter", func(ctx SpecContext, r Report) {
	common.CollectSuiteSupportBundle(ctx, *supportBundleDir)
	common.PostReport(ctx, r, *junitReport, *summaryReport)
	// Clean up perf test jobs after support bundle collection
})

// cleanupPerfJobs deletes perf test jobs by label selector.
func cleanupPerfJobs(ctx context.Context, ns string) {
	log.Info("Cleaning up test jobs")
	cmd := exec.CommandContext(ctx, "kubectl", "delete", "jobs",
		"-n", ns,
		"-l", "dacs.azure.com/test-pod=true",
		"--ignore-not-found=true")
	output, err := utils.Run(cmd)
	if err != nil {
		log.Error(err, "Failed to cleanup perf jobs", "output", output)
	}
}

// newPerfMetricBase creates a PerfMetric with common fields populated.
// The caller should set the Result field with the appropriate result type.
func newPerfMetricBase(ctx context.Context, podName, nodeName, nodeSKU string) *kusto.PerfMetric {
	clusterName, _ := cluster.GetCurrentContext()
	gitCommit := git.GetCommit(ctx)
	gitBranch := git.GetBranch(ctx)
	specReport := CurrentSpecReport()
	helmInfo := helm.GetReleaseInfo(ctx, namespace, "cache-server")

	return &kusto.PerfMetric{
		SuiteID:          suiteUUID,
		TestName:         specReport.FullText(),
		TestGitCommit:    gitCommit,
		TestGitBranch:    gitBranch,
		HelmChartVersion: helmInfo.ChartVersion,
		HelmAppVersion:   helmInfo.AppVersion,
		TestMetadata: kusto.TestMetadata{
			ClusterName: clusterName,
			PodName:     podName,
			NodeName:    nodeName,
			NodeSKU:     nodeSKU,
			IsMinikube:  cluster.IsMinikube(),
		},
	}
}
