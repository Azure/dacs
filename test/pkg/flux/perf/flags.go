/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Perf test specific flags
# --------------------------------------------------------------------------------------------------------------------
*/

package perf

import (
	"context"
	"flag"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/Azure/dacs/test/pkg/cluster"
	"github.com/Azure/dacs/test/pkg/flux"
)

// Performance test flags
type Options struct {
	// Common report/output flags
	*flux.Options

	// DataSizeGiB is the data size in GiB for perf tests
	DataSizeGiB int
	// CacheServerCount is the number of cache servers to use
	CacheServerCount int
	// NumThreads is the number of threads for perf client
	NumThreads int
	// ChecksumAlgorithm is the checksum algorithm (xxhash32 or crc32)
	ChecksumAlgorithm string

	// DACS client test flags
	// ClientImage is the DACS Cache Client Image to use for testing
	ClientImage string
	// StorageAccount is the name of the Azure Storage Account
	StorageAccount string
	// StorageContainer is the name of the Azure Storage Container
	StorageContainer string
	// WorkloadIdentityClientID is the Azure managed identity client ID for workload identity auth
	WorkloadIdentityClientID string
	// WorkloadIdentitySA is the Kubernetes service account name for workload identity
	WorkloadIdentitySA string
	// BlobCount is the number of blobs to write/read per client
	BlobCount uint
	// BlobSize is the size of each blob in bytes
	BlobSize uint64
	// WriteMode is the write mode (write_cache_only, write_blob_only, write_cache_blob_async, write_sync)
	WriteMode string
	// ClientCount is the number of cache clients (0 = auto-detect from nodes in cluster)
	ClientCount uint
}

// AddFlags adds performance test specific flags to the provided FlagSet
func AddFlags(fs *flag.FlagSet, opts *Options) error {
	var commonOpts flux.Options
	if err := flux.AddFlags(fs, &commonOpts); err != nil {
		return err
	}
	opts.Options = &commonOpts

	// Perfclient specific flags
	fs.IntVar(&opts.DataSizeGiB, "data-size-gib", 1, "Data size in GiB for perf tests")
	fs.IntVar(&opts.CacheServerCount, "cache-server-count", 1, "Number of cache servers to use")
	fs.IntVar(&opts.NumThreads, "num-threads", 1, "Number of threads for perf client")
	fs.StringVar(&opts.ChecksumAlgorithm, "checksum-algorithm", "xxhash32", "Checksum algorithm (xxhash32 or crc32)")

	// DACS client specific flags
	fs.StringVar(&opts.ClientImage, "client-image", "", "DACS Cache Client Image to use for testing")
	fs.StringVar(&opts.StorageAccount, "storage-account", "", "Azure Storage Account name (inferred from cluster context if not set)")
	fs.StringVar(&opts.StorageContainer, "storage-container", "test-data", "Azure Storage Container name")
	fs.StringVar(&opts.WorkloadIdentityClientID, "workload-identity-client-id", "", "Azure managed identity client ID for workload identity auth (inferred if not set)")
	fs.StringVar(&opts.WorkloadIdentitySA, "workload-identity-sa", "perf-client-sa", "Kubernetes service account name for workload identity")
	fs.UintVar(&opts.BlobCount, "blob-count", 50, "Number of blobs to write/read per client")
	fs.Uint64Var(&opts.BlobSize, "blob-size", 1073741824, "Size of each blob in bytes (1GB default)")
	fs.StringVar(&opts.WriteMode, "write-mode", "write_sync", "Write mode (write_cache_only, write_blob_only, write_cache_blob_async, write_sync)")
	fs.UintVar(&opts.ClientCount, "count", 0, "Number of cache clients (0 = auto-detect from nodes)")

	return nil
}

// ResolveDefaults infers values for flags that were not explicitly set.
// Call this after flag.Parse() so that inference only runs when needed.
func (o *Options) ResolveDefaults(ctx context.Context) {
	if o.StorageAccount == "" {
		o.StorageAccount = inferStorageAccountName()
	}
	if o.WorkloadIdentityClientID == "" {
		o.WorkloadIdentityClientID = inferWorkloadIdentityClientID(ctx)
	}
}

// inferStorageAccountName infers the storage account name from environment or cluster context.
// Checks STORAGE_ACCOUNT env var first, then falls back to inferring from cluster context.
// The storage account name follows the pattern from deploy/template/storage.bicep:
// toLower(take('${replace(resourceGroup().name, '-', ")}storage', 24))
// For AKS clusters, the context name is "{resource-group}-cluster", so we strip the "-cluster" suffix
// to get the resource group name.
func inferStorageAccountName() string {
	// Check environment variable first
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
	// Strip "-cluster" suffix if present (AKS cluster naming convention)
	resourceGroupName := strings.TrimSuffix(contextName, "-cluster")
	// Remove hyphens and append "storage", then truncate to 24 chars
	name := strings.ReplaceAll(resourceGroupName, "-", "") + "storage"
	if len(name) > 24 {
		name = name[:24]
	}
	return strings.ToLower(name)
}

// inferWorkloadIdentityClientID infers the workload identity client ID from the Azure resource group.
// Checks WORKLOAD_IDENTITY_CLIENT_ID env var first, then queries Azure for the managed identity.
// It looks for a managed identity named "perf-client-identity" (created by workload-identity.bicep)
// in the resource group derived from the cluster context name.
func inferWorkloadIdentityClientID(ctx context.Context) string {
	// Check environment variable first
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
	// Strip "-cluster" suffix if present (AKS cluster naming convention)
	resourceGroupName := strings.TrimSuffix(contextName, "-cluster")

	// Query Azure for the managed identity client ID using az CLI
	// The identity name matches what's defined in workload-identity.bicep
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
