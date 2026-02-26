/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Adapter benchmark test suite for DACS Python client
# --------------------------------------------------------------------------------------------------------------------
*/
package perf

import (
	"context"
	"fmt"
	"strings"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"

	"github.com/Azure/dacs/test/pkg/common"
)

const (
	GB                            = 1024 * 1024 * 1024
	adapterBenchmarkContainerName = "adapter-benchmark-"
)

var testAdapterBenchmark = func(ctx context.Context) {
	Expect(opts.StorageAccount).NotTo(BeEmpty(), "Storage account is required (--storage-account)")

	opts.ResolveDefaults(ctx)

	// TODO: Re-enable dynamic container creation after fixing az storage CLI access from pipeline agents.
	// now := time.Now().Format(time.RFC3339)
	// now = strings.ReplaceAll(strings.ToLower(now), ":", "-")
	// containerName := adapterBenchmarkContainerName + now
	// err := utils.CreateContainerIfNotExists(opts.StorageAccount, containerName)
	// Expect(err).NotTo(HaveOccurred(), "Failed to create container in storage account")
	// defer func() {
	// 	if err := utils.DeleteContainer(opts.StorageAccount, containerName); err != nil {
	// 		log.Error(err, "Failed to delete container", "container", containerName)
	// 	}
	// }()

	testImage := opts.ClientImage
	if testImage == "" {
		testImage = common.GetDefaultImage()
	}

	writeMode := opts.WriteMode
	if writeMode == "" {
		writeMode = "write_sync"
	}

	blobSizeGB := float64(opts.BlobSize) / float64(GB)
	if blobSizeGB == 0 {
		blobSizeGB = 1.0
	}

	numBlobs := int(opts.BlobCount)
	if numBlobs == 0 {
		numBlobs = 4
	}

	numProcesses := opts.NumProcesses
	if numProcesses == 0 {
		numProcesses = 1
	}

	config := &AdapterBenchmarkConfig{
		k8sClient:                k8sClient,
		workloadIdentityClientID: opts.WorkloadIdentityClientID,
		serviceAccountName:       opts.WorkloadIdentitySA,
		cacheServerNamespace:     namespace,
		storageAccountName:       opts.StorageAccount,
		containerName:            opts.StorageContainer,
		testImage:                testImage,
		clients:                  "blobfile,dacs,dacs_zero_copy",
		blobSizeGB:               blobSizeGB,
		numBlobs:                 numBlobs,
		numProcesses:             numProcesses,
		writeMode:                writeMode,
		cacheDiscoveryURL:        common.GetDiscoveryURL(namespace),
	}

	result, err := config.runBenchmark(ctx)
	Expect(err).NotTo(HaveOccurred(), "Failed to run adapter benchmark")

	// Log results
	log.Info(strings.Repeat("=", 80))
	log.Info("ADAPTER BENCHMARK RESULTS")
	log.Info(strings.Repeat("=", 80))
	for clientName, r := range result.Results {
		log.Info("Result",
			"client", clientName,
			"writeMbps", r.WriteThroughputMbps,
			"readMbps", r.ReadThroughputMbps,
		)
	}

	// Send metrics to Kusto
	sendAdapterBenchmarkMetrics(ctx, result)

	// Add report entries
	for clientName, r := range result.Results {
		AddReportEntry(clientName+" Write", ReportEntryVisibilityAlways,
			fmt.Sprintf("%.0f MB/s", r.WriteThroughputMbps))
		AddReportEntry(clientName+" Read", ReportEntryVisibilityAlways,
			fmt.Sprintf("%.0f MB/s", r.ReadThroughputMbps))
	}

	Expect(len(result.Results)).To(BeNumerically(">", 0), "No benchmark results")
}

var _ = Describe("Adapter Benchmark", Label("perf", "aks", "adapter"), Ordered, func() {
	Context("DACS Adapter Throughput Benchmark", Label("adapter"), func() {
		It("should run adapter throughput benchmark", testAdapterBenchmark)
	})
})
