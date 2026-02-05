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

// checksumConfig defines a test configuration for checksum testing.
type checksumConfig struct {
	name                    string
	enableCalculateChecksum bool // for upload
	enableValidateChecksum  bool // for download
	colocate                bool // schedule on same node as cache server
}

// randomPrefix generates a random prefix for test data to avoid collisions.
func randomPrefix(base string) string {
	return fmt.Sprintf("%s-%d", base, rand.Intn(100000))
}

var _ = Describe("PerfClient", Label("perf", "perfclient"), Ordered, func() {
	// Collect all results for the comparison report
	var results []PerfResult

	configs := []checksumConfig{
		{
			name:                    "checksum disabled",
			enableCalculateChecksum: false,
			enableValidateChecksum:  false,
		},
		{
			name:                    "checksum enabled",
			enableCalculateChecksum: true,
			enableValidateChecksum:  true,
		},
		{
			name:                    "colocated single server",
			enableCalculateChecksum: false,
			enableValidateChecksum:  false,
			colocate:                true,
		},
	}

	for _, cfg := range configs {
		Context(fmt.Sprintf("With %s", cfg.name), func() {
			It("should upload and download successfully", func(ctx context.Context) {
				// Generate a unique prefix for this test run
				prefix := randomPrefix("perf-test")

				// Upload phase
				By(fmt.Sprintf("Running upload with %s (prefix: %s)", cfg.name, prefix))
				uploadConfig := perfclient.NewConfig().
					WithMode(perfclient.ModeUpload).
					WithPrefix(prefix).
					WithCalculateChecksum(cfg.enableCalculateChecksum).
					WithColocate(cfg.colocate)

				uploadSummary := runPerfClientWithConfig(ctx, uploadConfig, cfg.colocate)
				Expect(uploadSummary).NotTo(BeNil(), "Failed to parse upload perf summary")
				Expect(uploadSummary.Mode).To(Equal("upload"))
				Expect(uploadSummary.TotalIndividualFailures).To(Equal(0), "Expected no upload failures")
				Expect(uploadSummary.ChunksSuccessfullyTransferred).To(Equal(uploadSummary.ExpectedChunksToTransfer), "All chunks should be uploaded successfully")

				results = append(results, PerfResult{Config: cfg.name, Mode: "upload", Summary: uploadSummary})

				// Download phase - uses same prefix to download what was uploaded
				By(fmt.Sprintf("Running download with %s (prefix: %s)", cfg.name, prefix))
				downloadConfig := perfclient.NewConfig().
					WithMode(perfclient.ModeDownload).
					WithPrefix(prefix).
					WithValidateChecksum(cfg.enableValidateChecksum).
					WithColocate(cfg.colocate)

				downloadSummary := runPerfClientWithConfig(ctx, downloadConfig, cfg.colocate)
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
		log.Info("Test Configuration", "dataSizeGiB", fluxOpts.DataSizeGiB, "cacheServerCount", fluxOpts.CacheServerCount, "numThreads", fluxOpts.NumThreads, "checksumAlgorithm", fluxOpts.ChecksumAlgorithm)
		log.Info(strings.Repeat("-", 80))
		for _, r := range results {
			log.Info("Result", "config", r.Config, "mode", r.Mode, "throughputMbps", r.Summary.ThroughputMbps, "durationMs", r.Summary.DurationMs, "failures", r.Summary.TotalIndividualFailures)
		}
		log.Info(strings.Repeat("=", 80))
	})
})

// runPerfClientWithConfig runs the perf client job with the given config and waits for completion.
// If colocate is true, it uses a single server; otherwise it uses the cacheServerCount flag value.
// Returns the parsed perfsummary.Summary from the job logs.
func runPerfClientWithConfig(ctx context.Context, config *perfclient.Config, colocate bool) *perfsummary.Summary {
	// Use single server for colocated tests, otherwise use the cacheServerCount flag value
	serverCount := fluxOpts.CacheServerCount
	if colocate {
		serverCount = 1
	}

	config.WithNamespace(namespace).
		WithServerHostname(buildServerHostname(namespace, serverCount)).
		WithDataSizeGiB(fluxOpts.DataSizeGiB).
		WithNumThreads(fluxOpts.NumThreads).
		WithChecksumAlgorithm(perfclient.ChecksumAlgorithm(fluxOpts.ChecksumAlgorithm))

	By(fmt.Sprintf("Creating %s perf client job (checksum calc=%t, validate=%t, colocate=%t, servers=%d)",
		config.Mode, config.EnableCalculateChecksum, config.EnableValidateChecksum, config.ColocateWithServer, serverCount))
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
	job.WaitForCompletion(ctx, k8sJob.Name, namespace, 5*time.Minute)

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
		NumServers:              serverCount,
		EnableCalculateChecksum: config.EnableCalculateChecksum,
		EnableValidateChecksum:  config.EnableValidateChecksum,
		ChecksumAlgorithm:       string(config.ChecksumAlgorithm),
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

// buildServerHostname builds the cache server hostname string for the given number of servers.
func buildServerHostname(ns string, numServers int) string {
	if numServers == 1 {
		return fmt.Sprintf("cacheserver-0.cacheserver.%s.svc.cluster.local", ns)
	}
	var hosts strings.Builder
	for i := range numServers {
		if i > 0 {
			hosts.WriteString("#")
		}
		fmt.Fprintf(&hosts, "cacheserver-%d.cacheserver.%s.svc.cluster.local", i, ns)
	}
	return hosts.String()
}
