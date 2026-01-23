package e2e

import (
	"context"
	"flag"
	"os"
	"path/filepath"
	"testing"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/client-go/kubernetes/scheme"
	"k8s.io/client-go/tools/clientcmd"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/client/config"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/logger"
)

var log = logger.New()

const namespace = "cache-server"

var (
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

	// Create scheme and register Cache CRD
	clientScheme := runtime.NewScheme()
	_ = scheme.AddToScheme(clientScheme)
	_ = storagev1.AddToScheme(clientScheme)

	k8sClient, err = client.New(cfg, client.Options{Scheme: clientScheme})
	if err != nil {
		log.Error(err, "Failed to create controller-runtime client")
		os.Exit(1)
	}

	os.Exit(m.Run())
}

// TestE2E runs the end-to-end (e2e) test suite for DACS. These tests
// execute in an isolated, temporary environment to validate project changes.
func TestE2E(t *testing.T) {
	RegisterFailHandler(Fail)
	log.Info("Starting DACS E2E test suite")
	RunSpecs(t, "e2e suite")
}

var _ = BeforeSuite(func(ctx context.Context) {
	common.Setup(ctx, k8sClient, namespace)
})

var _ = AfterSuite(func(ctx context.Context) {
	common.Teardown(ctx, k8sClient, namespace, *supportBundleDir)
})

var _ = ReportAfterSuite("e2e reporter", func(ctx SpecContext, r Report) {
	common.CollectSuiteSupportBundle(ctx, *supportBundleDir)
	common.PostReport(ctx, r, *junitReport, *summaryReport)
})
