/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Common flags for all test suites
# --------------------------------------------------------------------------------------------------------------------
*/

package flux

import (
	"context"
	"flag"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/Azure/dacs/test/pkg/cluster"
)

// Options holds common flags for flux-based tests
type Options struct {
	// JunitReport is the report file generated for consumption by Junit.
	JunitReport string
	// SupportBundleDir is the directory where support bundles are written.
	SupportBundleDir string
	// SummaryReport is the path to write the summary report
	SummaryReport string
	// PytestOutput is the path to write pytest JUnit XML report
	PytestOutput string
	// StorageAccount is the name of the Azure Storage Account
	StorageAccount string
	// ClientImage is the test client image to use
	ClientImage string
	// WorkloadIdentityClientID is the Azure managed identity client ID for workload identity auth
	WorkloadIdentityClientID string
	// WorkloadIdentitySA is the Kubernetes service account name for workload identity
	WorkloadIdentitySA string
}

// AddFlags adds common flux test flags to the provided FlagSet
func AddFlags(fs *flag.FlagSet, opts *Options) error {
	fs.StringVar(&opts.JunitReport, "junit-report", "", "The report file generated for consumption by Junit.")
	fs.StringVar(&opts.SupportBundleDir, "support-bundle-dir", "", "The directory where support bundles are written.")
	fs.StringVar(&opts.SummaryReport, "summary-report", "", "The path to write the summary report")
	fs.StringVar(&opts.PytestOutput, "pytest-output", "", "Path to write pytest JUnit XML report (optional)")
	fs.StringVar(&opts.StorageAccount, "storage-account", "", "Azure Storage Account name (inferred from cluster context if not set)")
	fs.StringVar(&opts.ClientImage, "client-image", "", "Test client image to use")
	fs.StringVar(&opts.WorkloadIdentityClientID, "workload-identity-client-id", "", "Azure managed identity client ID for workload identity auth (inferred if not set)")
	fs.StringVar(&opts.WorkloadIdentitySA, "workload-identity-sa", "perf-client-sa", "Kubernetes service account name for workload identity")

	return nil
}

// ResolveDefaults infers values for flags that were not explicitly set.
// Call this after flag.Parse() so that inference only runs when needed.
func (o *Options) ResolveDefaults(ctx context.Context) {
	if o.StorageAccount == "" {
		o.StorageAccount = InferStorageAccountName()
	}
	if o.WorkloadIdentityClientID == "" {
		o.WorkloadIdentityClientID = InferWorkloadIdentityClientID(ctx)
	}
}

// InferStorageAccountName infers the storage account name from environment or cluster context.
func InferStorageAccountName() string {
	if sa := os.Getenv("STORAGE_ACCOUNT"); sa != "" {
		return sa
	}

	if cluster.IsMinikube() {
		return ""
	}
	contextName, err := cluster.GetCurrentContext()
	if err != nil {
		return ""
	}
	resourceGroupName := strings.TrimSuffix(contextName, "-cluster")
	name := strings.ReplaceAll(resourceGroupName, "-", "") + "storage"
	if len(name) > 24 {
		name = name[:24]
	}
	return strings.ToLower(name)
}

// InferWorkloadIdentityClientID infers the workload identity client ID from Azure.
func InferWorkloadIdentityClientID(ctx context.Context) string {
	if clientID := os.Getenv("WORKLOAD_IDENTITY_CLIENT_ID"); clientID != "" {
		return clientID
	}

	if cluster.IsMinikube() {
		return ""
	}
	contextName, err := cluster.GetCurrentContext()
	if err != nil {
		return ""
	}
	resourceGroupName := strings.TrimSuffix(contextName, "-cluster")

	queryCtx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()

	cmd := exec.CommandContext(queryCtx, "az", "identity", "show",
		"--resource-group", resourceGroupName,
		"--name", "perf-client-identity",
		"--query", "clientId",
		"--output", "tsv")
	output, err := cmd.Output()
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(output))
}
