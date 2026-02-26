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

	// For sake of cacheintegration pipeline we are pointing to perf options
	e2eflags "github.com/Azure/dacs/test/pkg/flux/perf"
	"github.com/Azure/dacs/test/pkg/logger"
)

var log = logger.New()

var namespace = common.GetNamespace()

var (
	// k8sClient is the controller-runtime client for interacting with the cluster.
	k8sClient client.Client

	// defaultKubeConfigPath is the default path to the kubeconfig file.
	defaultKubeConfigPath = filepath.Join(os.Getenv("HOME"), ".kube", "config")
	// fluxOpts holds the e2e test options
	fluxOpts e2eflags.Options
)

func TestMain(m *testing.M) {
	if err := e2eflags.AddFlags(flag.CommandLine, &fluxOpts); err != nil {
		log.Error(err, "failed to add flux flags")
		os.Exit(1)
	}

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
	common.Teardown(ctx, k8sClient, namespace, fluxOpts.SupportBundleDir)
})

var _ = ReportAfterSuite("e2e reporter", func(ctx SpecContext, r Report) {
	common.CollectSuiteSupportBundle(ctx, fluxOpts.SupportBundleDir)
	common.PostReport(ctx, r, fluxOpts.JunitReport, fluxOpts.SummaryReport)

	// Write collected pytest results if any
	if fluxOpts.PytestOutput != "" {
		if err := WritePytestResults(fluxOpts.PytestOutput); err != nil {
			log.Error(err, "Failed to write pytest JUnit XML")
		}
	}
})
