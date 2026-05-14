/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Adapter E2E test suite for DACS Python client
# --------------------------------------------------------------------------------------------------------------------
*/
package e2e

import (
	"context"
	"fmt"
	"os"
	"strings"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"

	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/utils"
)

// adapterConfigs defines all pytest adapter test configurations.
var adapterConfigs = []PytestAdapterConfig{
	{
		AdapterName: "BlobFile",
		TestType:    TestTypeBlobFile,
		Labels:      []string{"aks", "adapter", "blobfile"},
	},
	{
		AdapterName: "BoostedBlob",
		TestType:    TestTypeBoostedBlob,
		Labels:      []string{"aks", "adapter", "boostedblob"},
	},
}

// Register test specs for each adapter (cache enabled by default)
var _ = func() bool {
	for _, cfg := range adapterConfigs {
		DescribePytestAdapterTests(cfg, true)
	}
	return true
}()

// DescribePytestAdapterTests generates test specs for a given adapter and cache mode.
func DescribePytestAdapterTests(cfg PytestAdapterConfig, cacheEnabled bool) {
	modeLabel := "cache-disabled"
	modeDesc := "without cache"
	if cacheEnabled {
		modeLabel = "cache-enabled"
		modeDesc = "with cache"
	}

	var config *DACSAdapterTestConfig

	labels := append([]string{"e2e", modeLabel}, cfg.Labels...)
	Describe(fmt.Sprintf("%s Adapter (%s)", cfg.AdapterName, modeDesc), Label(labels...), Ordered, func() {
		BeforeAll(func(ctx context.Context) {
			Expect(fluxOpts.StorageAccount).NotTo(BeEmpty(), "Storage account is required (--storage-account)")

			fluxOpts.ResolveDefaults(ctx)

			// Determine auth mode: workload identity > SAS token > managed identity
			var clientID, sasToken string
			if fluxOpts.WorkloadIdentityClientID == "" || fluxOpts.WorkloadIdentitySA == "" {
				// No workload identity — fall back to SAS or managed identity
				accessEnvKey, accessEnvValue, err := common.GetStorageAccessEnv(ctx, k8sClient, fluxOpts.StorageAccount)
				Expect(err).NotTo(HaveOccurred(), "Failed to get storage access credentials")

				if accessEnvKey == common.CACHECLIENT_SAS_TOKEN_KEY {
					sasToken = accessEnvValue
				} else {
					clientID = accessEnvValue
				}
			}

			// Generate unique container name with timestamp
			now := time.Now().Format(time.RFC3339)
			now = strings.ReplaceAll(strings.ToLower(now), ":", "-")
			containerName := adapterE2EContainerNamePrefix + now

			// Create container if it doesn't exist
			err := utils.CreateContainerIfNotExists(fluxOpts.StorageAccount, containerName)
			Expect(err).NotTo(HaveOccurred(), "Failed to create container in storage account")

			config = &DACSAdapterTestConfig{
				k8sClient:                k8sClient,
				workloadIdentityClientID: fluxOpts.WorkloadIdentityClientID,
				serviceAccountName:       fluxOpts.WorkloadIdentitySA,
				namespace:                namespace,
				image:                    getAdapterTestImage(),
				storageAccount:           fluxOpts.StorageAccount,
				containerName:            containerName,
				clientID:                 clientID,
				sasToken:                 sasToken,
				cacheEnabled:             cacheEnabled,
				cacheDiscoveryURL:        common.GetDiscoveryURLForCache(cacheName, namespace),
				pytestOutput:             fluxOpts.PytestOutput,
			}

			if fluxOpts.WorkloadIdentityClientID == "" && sasToken == "" {
				Expect(config.clientID).NotTo(BeEmpty(), "Client ID is required (from node labels)")
			}

			authMode := "workload-identity"
			if config.sasToken != "" {
				authMode = "sas-token"
			} else if config.clientID != "" {
				authMode = "managed-identity"
			}

			log.Info(fmt.Sprintf("%s test configuration", cfg.AdapterName),
				"namespace", config.namespace,
				"storageAccount", config.storageAccount,
				"containerName", config.containerName,
				"cacheEnabled", config.cacheEnabled,
				"authMode", authMode,
			)

			Expect(config.deploy(ctx)).To(Succeed(), "Failed to deploy Python client pod")
		})

		AfterAll(func(ctx context.Context) {
			if os.Getenv("SKIP_CLEANUP") != "true" {
				Expect(config.cleanup(ctx)).To(Succeed(), "Failed to cleanup Python client resources")
			}

			// Cleanup container
			if config != nil && config.containerName != "" {
				if err := utils.DeleteContainer(config.storageAccount, config.containerName); err != nil {
					log.Error(err, "Failed to delete container", "container", config.containerName)
				}
			}
		})

		Context(fmt.Sprintf("%s API Tests", cfg.AdapterName), Label(fmt.Sprintf("%s-api", cfg.Labels[0])), func() {
			BeforeAll(func(ctx context.Context) {
				Expect(config.copyTestFiles(ctx, cfg.TestType)).To(Succeed())
			})

			It(fmt.Sprintf("should run all %s pytest cases", cfg.Labels[0]), func(ctx context.Context) {
				result, err := config.runTests(ctx, cfg.TestType)
				Expect(err).NotTo(HaveOccurred(), "Failed to execute %s tests", cfg.Labels[0])

				for _, testName := range result.PassedTests {
					AddReportEntry(fmt.Sprintf("[%s] %s", modeLabel, testName), ReportEntryVisibilityAlways, "PASSED ✓")
				}
				for _, testName := range result.FailedTests {
					AddReportEntry(fmt.Sprintf("[%s] %s", modeLabel, testName), ReportEntryVisibilityAlways, "FAILED ✗")
				}

				AddReportEntry(fmt.Sprintf("[%s] %s Summary", modeLabel, cfg.AdapterName), ReportEntryVisibilityAlways,
					fmt.Sprintf("Total: %d, Passed: %d, Failed: %d, Skipped: %d",
						result.Passed+result.Failed+result.Skipped,
						result.Passed, result.Failed, result.Skipped))

				if result.Failed > 0 || result.Errors > 0 || result.ExitCode != 0 {
					log.Info(fmt.Sprintf("%s test output", cfg.AdapterName), "cacheEnabled", cacheEnabled, "output", result.Output)
				}

				Expect(result.ExitCode).To(Equal(0), "%s pytest exited with error code %d. Output: %s", cfg.AdapterName, result.ExitCode, result.Output)
				Expect(result.Passed+result.Failed+result.Skipped).To(BeNumerically(">", 0), "No tests ran - pytest may have crashed during collection")
				Expect(result.Failed).To(Equal(0), "%s tests failed: %v", cfg.AdapterName, result.FailedTests)
				Expect(result.Errors).To(Equal(0), "%s tests had errors", cfg.AdapterName)
			})
		})
	})
}
