/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Model loading scenario performance tests
# --------------------------------------------------------------------------------------------------------------------
*/
package perf

import (
	"context"
	"fmt"
	"strings"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"

	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/kusto"
	"github.com/Azure/dacs/test/pkg/scenario"
)

func scenarioConfig() scenario.ModelLoadingConfig {
	return scenario.ModelLoadingConfig{
		Namespace:                namespace,
		StorageAccount:           opts.StorageAccount,
		StorageContainer:         opts.StorageContainer,
		TestImage:                getClientImage(),
		WorkloadIdentityClientID: opts.WorkloadIdentityClientID,
		WorkloadIdentitySA:       opts.WorkloadIdentitySA,
		CacheDiscoveryURL:        common.GetDiscoveryURL(namespace),
	}
}

func runModelLoadingTest(ctx context.Context, scale scenario.ScaleProfile, method scenario.AccessMethod, cache scenario.CacheMode) {
	config := scenarioConfig()
	Expect(config.StorageAccount).NotTo(BeEmpty(), "Storage account is required (--storage-account)")

	// Cache setup: flush before cold_cache to guarantee a clean start.
	// warm_cache relies on the preceding cold_cache run having populated the cache
	// (test order is no_cache → cold_cache → warm_cache).
	if cache == scenario.ColdCache {
		By("Flushing all cache data")
		if err := scenario.FlushCache(ctx, config.CacheDiscoveryURL); err != nil {
			log.Info("FlushCache warning (non-fatal)", "error", err)
		}
		time.Sleep(5 * time.Second)
	}

	runner := &scenario.ModelLoadingRunner{
		Config:    config,
		K8sClient: k8sClient,
		Scale:     scale,
		Method:    method,
		Cache:     cache,
	}

	result, err := runner.Run(ctx)
	Expect(err).NotTo(HaveOccurred(), "Model loading scenario failed")
	Expect(result).NotTo(BeNil())
	Expect(result.TotalSizeGB).To(BeNumerically(">", 0), "No data was read")
	Expect(result.ThroughputMbps).To(BeNumerically(">", 0), "Zero throughput")

	// Log results
	log.Info(strings.Repeat("=", 60))
	log.Info("MODEL LOADING RESULT",
		"method", result.AccessMethod,
		"cache", result.CacheMode,
		"scale", result.Scale,
		"shards", result.ShardCount,
		"sizeGB", fmt.Sprintf("%.2f", result.TotalSizeGB),
		"timeSec", fmt.Sprintf("%.1f", result.TotalLoadTimeSec),
		"throughputMbps", fmt.Sprintf("%.0f", result.ThroughputMbps),
	)
	log.Info(strings.Repeat("=", 60))

	// Ginkgo report entries (visible in test output)
	AddReportEntry("Throughput", ReportEntryVisibilityAlways,
		fmt.Sprintf("%.0f Mbps", result.ThroughputMbps))
	AddReportEntry("Total Size", ReportEntryVisibilityAlways,
		fmt.Sprintf("%.2f GB", result.TotalSizeGB))
	AddReportEntry("Load Time", ReportEntryVisibilityAlways,
		fmt.Sprintf("%.1f s", result.TotalLoadTimeSec))
	AddReportEntry("Shards", ReportEntryVisibilityAlways,
		fmt.Sprintf("%d", result.ShardCount))

	// Publish metrics (prints always; sends to EventHub only if SEND_METRICS=true)
	metric := newPerfMetricBase(ctx, "", "", "")
	metric.Result = kusto.ScenarioResult{
		Scenario:         result.Scenario,
		AccessMethod:     result.AccessMethod,
		CacheMode:        result.CacheMode,
		Scale:            result.Scale,
		HFRepo:           result.HFRepo,
		ShardCount:       result.ShardCount,
		TotalSizeGB:      result.TotalSizeGB,
		TotalLoadTimeSec: result.TotalLoadTimeSec,
		ThroughputMbps:   result.ThroughputMbps,
		CacheHitRate:     result.CacheHitRate,
	}
	if err := kusto.SendPerfMetric(ctx, kusto.DefaultEventHubConfig, metric); err != nil {
		log.Error(err, "Failed to send scenario metric (non-fatal)")
	}
}

var _ = Describe("Model Loading Scenario", Label("aks", "scenario", "model-loading"), Ordered, func() {
	var (
		scaleProfile scenario.ScaleProfile
		config       scenario.ModelLoadingConfig
	)

	BeforeAll(func(ctx context.Context) {
		config = scenarioConfig()
		Expect(config.StorageAccount).NotTo(BeEmpty(), "Storage account is required")

		var err error
		scaleProfile, err = scenario.ScaleByName(opts.ScenarioScale)
		Expect(err).NotTo(HaveOccurred(), "Invalid scenario scale")

		log.Info("Model Loading Scenario",
			"scale", scaleProfile.Name,
			"hfRepo", scaleProfile.HFRepo,
			"methods", opts.ScenarioMethods,
			"cacheModes", opts.ScenarioCacheModes)

		// Provision model data in blob storage
		By("Provisioning model data from HF to blob storage")
		err = scenario.ProvisionModelData(ctx, k8sClient, config, scaleProfile)
		Expect(err).NotTo(HaveOccurred(), "Failed to provision model data")
	})

	// Parse requested methods and cache modes
	methods := parseAccessMethods(opts.ScenarioMethods)
	cacheModes := parseCacheModes(opts.ScenarioCacheModes)

	for _, method := range methods {
		Context(fmt.Sprintf("AccessMethod=%s", method), func() {
			for _, cacheMode := range cacheModes {
				// Skip invalid combinations
				if !isValidCombination(method, cacheMode) {
					continue
				}

				It(fmt.Sprintf("CacheMode=%s", cacheMode), func(ctx context.Context) {
					scale, err := scenario.ScaleByName(opts.ScenarioScale)
					Expect(err).NotTo(HaveOccurred())
					runModelLoadingTest(ctx, scale, method, cacheMode)
				})
			}
		})
	}
})

func parseAccessMethods(s string) []scenario.AccessMethod {
	if s == "" {
		return []scenario.AccessMethod{scenario.Blobfile}
	}
	var methods []scenario.AccessMethod
	for _, part := range strings.Split(s, ",") {
		part = strings.TrimSpace(part)
		if part != "" {
			methods = append(methods, scenario.AccessMethod(part))
		}
	}
	return methods
}

func parseCacheModes(s string) []scenario.CacheMode {
	if s == "" {
		return []scenario.CacheMode{scenario.NoCache}
	}
	var modes []scenario.CacheMode
	for _, part := range strings.Split(s, ",") {
		part = strings.TrimSpace(part)
		if part != "" {
			modes = append(modes, scenario.CacheMode(part))
		}
	}
	return modes
}

// isValidCombination checks whether an access method supports a given cache mode.
func isValidCombination(method scenario.AccessMethod, cache scenario.CacheMode) bool {
	switch method {
	case scenario.HFDownloader:
		// HF downloader only supports no_cache (downloads from HF directly)
		return cache == scenario.NoCache
	case scenario.Blobfile:
		// Plain blobfile for no_cache; dacs_blobfile_patcher for cache modes
		return true
	case scenario.PythonAdapter:
		// Workarounds: FQDN account name for endpoint, SI_azBlobAccessToken for auth,
		// explicit offset reads. Supports no_cache (remote blob) and cache modes.
		return cache == scenario.NoCache || cache == scenario.ColdCache || cache == scenario.WarmCache
	case scenario.Interceptor:
		// Uses libStorageIntercept.so (LD_PRELOAD) with non-dynamic config,
		// explicit azBlobStorageEndpoint, and access token auth.
		return cache == scenario.NoCache || cache == scenario.ColdCache || cache == scenario.WarmCache
	case scenario.StreamClient:
		// Stream client supports all cache modes
		return true
	default:
		return cache == scenario.NoCache
	}
}
