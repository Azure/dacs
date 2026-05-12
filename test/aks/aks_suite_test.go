package aks

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"testing"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"k8s.io/client-go/tools/clientcmd"

	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/flux/perf"
)

// Use the perf flag superset so the AKS suite accepts the same `--` flags as
// the perf and e2e suites when all three are run together via `ginkgo -r`.
// The AKS suite ignores the perf-specific values; registering them just
// prevents Go's flag parser from rejecting unknown flags.
var fluxOpts perf.Options

func TestMain(m *testing.M) {
	// TestMain runs before Ginkgo bootstraps, so GinkgoLogr is a no-op here.
	// Use stderr directly for the only two failure paths (both fatal).
	if err := perf.AddFlags(flag.CommandLine, &fluxOpts); err != nil {
		fmt.Fprintf(os.Stderr, "failed to add flux flags: %v\n", err)
		os.Exit(1)
	}

	if os.Getenv(clientcmd.RecommendedConfigPathEnvVar) == "" {
		defaultPath := filepath.Join(os.Getenv("HOME"), ".kube", "config")
		if err := os.Setenv(clientcmd.RecommendedConfigPathEnvVar, defaultPath); err != nil {
			fmt.Fprintf(os.Stderr, "failed to set default kubeconfig path: %v\n", err)
			os.Exit(1)
		}
	}

	flag.Parse()
	os.Exit(m.Run())
}

func TestAKS(t *testing.T) {
	RegisterFailHandler(Fail)
	RunSpecs(t, "aks suite")
}

var _ = ReportAfterSuite("aks reporter", func(ctx SpecContext, r Report) {
	common.CollectSuiteSupportBundle(ctx, fluxOpts.SupportBundleDir)
	common.PostReport(ctx, r, fluxOpts.JunitReport, fluxOpts.SummaryReport)
})
