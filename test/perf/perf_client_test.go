package perf

import (
	"context"
	"fmt"
	"math/rand"
	"os/exec"
	"strings"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"

	"github.com/Azure/dacs/test/pkg/kube/job"
	"github.com/Azure/dacs/test/pkg/kusto"
	"github.com/Azure/dacs/test/pkg/perfclient"
	"github.com/Azure/dacs/test/pkg/perfsummary"
	"github.com/Azure/dacs/test/pkg/utils"
)

// PerfResult holds results for a single test run.
type PerfResult struct {
	Config  string
	Mode    string
	Summary *perfsummary.Summary
}

// perfTestConfig defines a test configuration for perf client testing.
type perfTestConfig struct {
	name                    string
	label                   Labels                // ginkgo label for filtering tests
	enableCalculateChecksum bool                  // for upload
	enableValidateChecksum  bool                  // for download
	colocate                bool                  // schedule on same node as cache server
	clientType              perfclient.ClientType // cache or direct-blob
	chunkSizeBytes          int                   // optional override for chunk size (0 = default)
	jobTimeout              time.Duration         // per-job timeout (0 = defaultJobTimeout)
}

const defaultJobTimeout = 10 * time.Minute

// newPerfConfig creates a fully configured perfclient.Config from this test configuration.
func (c *perfTestConfig) newPerfConfig(mode perfclient.Mode, prefix string) *perfclient.Config {
	// Use single server for colocated tests, otherwise use flag value
	serverCount := opts.CacheServerCount
	if c.colocate {
		serverCount = 1
	}

	config := perfclient.NewConfig().
		WithClientType(c.clientType).
		WithMode(mode).
		WithPrefix(prefix).
		WithColocate(c.colocate).
		WithCacheName(cacheName).
		WithNamespace(namespace).
		WithDataSizeGiB(opts.DataSizeGiB).
		WithNumThreads(opts.NumThreads)

	if c.chunkSizeBytes > 0 {
		config.WithChunkSizeBytes(c.chunkSizeBytes)
	}

	if mode == perfclient.ModeUpload {
		config.WithCalculateChecksum(c.enableCalculateChecksum)
	} else {
		config.WithValidateChecksum(c.enableValidateChecksum)
	}

	if c.clientType == perfclient.ClientTypeDirectBlob {
		config.WithStorageAccount(opts.StorageAccount).
			WithStorageContainer(opts.StorageContainer).
			WithWorkloadIdentity(opts.WorkloadIdentityClientID, opts.WorkloadIdentitySA)
	} else {
		config.WithNumServers(serverCount).
			WithChecksumAlgorithm(perfclient.ChecksumAlgorithm(opts.ChecksumAlgorithm))

		if c.colocate {
			// Colocated tests target specific servers — disable discovery and use explicit hostnames
			config.WithServerHostname(buildServerHostname(cacheName, namespace, serverCount)).
				WithDiscoveryEnabled(false)
		}
		// Otherwise, discovery is enabled by default — no need to specify hostnames
	}

	return config
}

// randomPrefix generates a random prefix for test data to avoid collisions.
func randomPrefix(base string) string {
	return fmt.Sprintf("%s-%d", base, rand.Intn(100000))
}

var _ = Describe("PerfClient", Label("perf", "perfclient"), Ordered, func() {
	// Collect all results for the comparison report
	var results []PerfResult

	configs := []perfTestConfig{
		{
			name:       "checksum disabled",
			label:      Label("cache"),
			clientType: perfclient.ClientTypeCache,
		},
		{
			name:           "checksum disabled - 64KiB chunk",
			label:          Label("cache", "small-chunk"),
			clientType:     perfclient.ClientTypeCache,
			chunkSizeBytes: 64 << 10, // 64 KiB
		},
		{
			name:                    "checksum enabled",
			label:                   Label("cache"),
			enableCalculateChecksum: true,
			enableValidateChecksum:  true,
			clientType:              perfclient.ClientTypeCache,
		},
		{
			name:       "colocated single server",
			label:      Label("cache"),
			colocate:   true,
			clientType: perfclient.ClientTypeCache,
		},
		{
			name:       "direct blob storage",
			label:      Label("directblob", "aks"),
			clientType: perfclient.ClientTypeDirectBlob,
		},
		{
			name:           "direct blob storage - 64KiB chunk",
			label:          Label("directblob", "aks", "small-chunk"),
			clientType:     perfclient.ClientTypeDirectBlob,
			chunkSizeBytes: 64 << 10,        // 64 KiB
			jobTimeout:     30 * time.Minute, // small chunks need more time due to higher round-trip count
		},
	}

	for _, cfg := range configs {
		Context(fmt.Sprintf("With %s", cfg.name), cfg.label, func() {

			It("should upload and download successfully", func(ctx context.Context) {

				// Generate a unique prefix for this test run
				prefix := randomPrefix("perf-test")

				// Upload phase
				By(fmt.Sprintf("Running upload with %s (prefix: %s)", cfg.name, prefix))
				uploadConfig := cfg.newPerfConfig(perfclient.ModeUpload, prefix)
				uploadSummary := runPerfClientWithConfig(ctx, uploadConfig, cfg.colocate, cfg.getJobTimeout())
				Expect(uploadSummary).NotTo(BeNil(), "Failed to parse upload perf summary")
				Expect(uploadSummary.Mode).To(Equal("upload"))
				Expect(uploadSummary.TotalIndividualFailures).To(Equal(0), "Expected no upload failures")
				Expect(uploadSummary.ChunksSuccessfullyTransferred).To(Equal(uploadSummary.ExpectedChunksToTransfer), "All chunks should be uploaded successfully")

				results = append(results, PerfResult{Config: cfg.name, Mode: "upload", Summary: uploadSummary})

				// Download phase - uses same prefix to download what was uploaded
				By(fmt.Sprintf("Running download with %s (prefix: %s)", cfg.name, prefix))
				downloadConfig := cfg.newPerfConfig(perfclient.ModeDownload, prefix)
				downloadSummary := runPerfClientWithConfig(ctx, downloadConfig, cfg.colocate, cfg.getJobTimeout())
				Expect(downloadSummary).NotTo(BeNil(), "Failed to parse download perf summary")
				Expect(downloadSummary.Mode).To(Equal("download"))
				Expect(downloadSummary.TotalIndividualFailures).To(Equal(0), "Expected no download failures")
				Expect(downloadSummary.ChunksSuccessfullyTransferred).To(Equal(downloadSummary.ExpectedChunksToTransfer), "All chunks should be downloaded successfully")

				results = append(results, PerfResult{Config: cfg.name, Mode: "download", Summary: downloadSummary})
			})
		})
	}

	AfterAll(func() {
		if len(results) == 0 {
			return
		}

		log.Info(strings.Repeat("=", 80))
		log.Info("PERFORMANCE TEST SUMMARY")
		log.Info(strings.Repeat("=", 80))
		log.Info("Test Configuration", "dataSizeGiB", opts.DataSizeGiB, "cacheServerCount", opts.CacheServerCount, "numThreads", opts.NumThreads, "checksumAlgorithm", opts.ChecksumAlgorithm)
		log.Info(strings.Repeat("-", 80))
		for _, r := range results {
			log.Info("Result", "config", r.Config, "mode", r.Mode, "throughputMbps", r.Summary.ThroughputMbps, "durationMs", r.Summary.DurationMs, "failures", r.Summary.TotalIndividualFailures)
		}
		log.Info(strings.Repeat("=", 80))
	})
})

// getJobTimeout returns the configured job timeout or the default.
func (c *perfTestConfig) getJobTimeout() time.Duration {
	if c.jobTimeout > 0 {
		return c.jobTimeout
	}
	return defaultJobTimeout
}

// runPerfClientWithConfig runs the perf client job with the given config and waits for completion.
// Returns the parsed perfsummary.Summary from the job logs.
func runPerfClientWithConfig(ctx context.Context, config *perfclient.Config, colocate bool, timeout time.Duration) *perfsummary.Summary {
	By(fmt.Sprintf("Creating %s perf client job (checksum calc=%t, validate=%t, colocate=%t, servers=%d)",
		config.Mode, config.EnableCalculateChecksum, config.EnableValidateChecksum, config.ColocateWithServer, config.NumServers))

	// If workload identity is configured, ensure the ServiceAccount exists
	if saYAML, err := config.ServiceAccountYAML(); err == nil && saYAML != nil {
		By("Ensuring workload identity ServiceAccount exists")
		cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
		cmd.Stdin = utils.NewStringReader(string(saYAML))
		output, err := utils.Run(cmd)
		Expect(err).NotTo(HaveOccurred(), "Failed to apply ServiceAccount: %s", output)
	}

	k8sJob, err := config.LoadJob()
	Expect(err).NotTo(HaveOccurred(), "Failed to load perf client job")

	// Delete any existing job first
	_ = job.Delete(ctx, k8sJob.Name, namespace)

	// Apply the job using kubectl
	jobYAML, err := config.JobYAML()
	Expect(err).NotTo(HaveOccurred(), "Failed to generate job YAML")

	cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
	cmd.Stdin = utils.NewStringReader(string(jobYAML))
	output, err := utils.Run(cmd)
	Expect(err).NotTo(HaveOccurred(), "Failed to apply perf client job: %s", output)

	By(fmt.Sprintf("Waiting for %s job to complete", config.Mode))
	job.WaitForCompletion(ctx, k8sJob.Name, namespace, timeout)

	By(fmt.Sprintf("Parsing %s job logs", config.Mode))
	jobLogs, err := job.GetLogs(ctx, k8sJob.Name, namespace)
	Expect(err).NotTo(HaveOccurred(), "Failed to get job logs")
	summary, err := perfsummary.Parse(jobLogs)
	Expect(err).NotTo(HaveOccurred(), "Failed to parse perf summary from job logs")
	log.Info("Performance Summary", "job", k8sJob.Name, "summary", summary.String())

	// Log and optionally send metrics to Kusto via EventHub
	By("Logging performance metrics")
	podName, nodeName, nodeSKU := job.GetPodInfo(ctx, k8sJob.Name, namespace)
	metric := newPerfMetricBase(ctx, podName, nodeName, nodeSKU)
	metric.Result = kusto.PerfResult{
		Mode:                    summary.Mode,
		ChunkSizeBytes:          summary.ChunkSizeBytes,
		ClientType:              string(config.ClientType),
		DurationMs:              summary.DurationMs,
		TotalBytesTransferred:   summary.TotalBytesTransferred,
		ThroughputMbps:          summary.ThroughputMbps,
		TotalFailures:           summary.TotalIndividualFailures,
		ExpectedChunks:          summary.ExpectedChunksToTransfer,
		SuccessfulChunks:        summary.ChunksSuccessfullyTransferred,
		PerChunkP50Ms:           summary.PerChunkP50Ms,
		PerChunkP90Ms:           summary.PerChunkP90Ms,
		PerChunkP99Ms:           summary.PerChunkP99Ms,
		NumThreads:              config.NumThreads,
		DataSizeGiB:             config.DataSizeGiB,
		NumIterations:           config.NumIterations,
		NumServers:              config.NumServers,
		EnableCalculateChecksum: config.EnableCalculateChecksum,
		EnableValidateChecksum:  config.EnableValidateChecksum,
		ChecksumAlgorithm:       string(config.ChecksumAlgorithm),
		StorageAccount:          config.StorageAccount,
		StorageContainer:        config.StorageContainer,
	}
	// SendPerfMetric always prints the metric; only sends to EventHub if SEND_METRICS=true
	err = kusto.SendPerfMetric(ctx, kusto.DefaultEventHubConfig, metric)
	if err != nil {
		log.Error(err, "Failed to send perf metric (non-fatal)")
	}

	// Note: We don't delete the job here. The job has ttlSecondsAfterFinished=300
	// which gives time for support bundle collection before auto-cleanup.
	// Support bundle is collected in AfterSuite before the TTL expires.

	return summary
}

// buildServerHostname builds the cache server hostname string for the given
// cache name and number of servers. The headless service has the same name as
// the cache, so pod DNS follows the pattern <cacheName>-N.<cacheName>.<ns>...
func buildServerHostname(name, ns string, numServers int) string {
	if numServers == 1 {
		return fmt.Sprintf("%s-0.%s.%s.svc.cluster.local", name, name, ns)
	}
	var hosts strings.Builder
	for i := range numServers {
		if i > 0 {
			hosts.WriteString("#")
		}
		fmt.Fprintf(&hosts, "%s-%d.%s.%s.svc.cluster.local", name, i, name, ns)
	}
	return hosts.String()
}
