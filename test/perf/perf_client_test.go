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

	"github.com/Azure/dacs/test/pkg/cluster"
	"github.com/Azure/dacs/test/pkg/git"
	"github.com/Azure/dacs/test/pkg/helm"
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
					WithCalculateChecksum(cfg.enableCalculateChecksum)

				uploadSummary := runPerfClientWithConfig(ctx, uploadConfig)
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
					WithValidateChecksum(cfg.enableValidateChecksum)

				downloadSummary := runPerfClientWithConfig(ctx, downloadConfig)
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
		log.Info("Test Configuration", "dataSizeGiB", *dataSizeGiB, "cacheServerCount", *cacheServerCount, "numThreads", *numThreads, "checksumAlgorithm", *checksumAlgorithm)
		log.Info(strings.Repeat("-", 80))
		for _, r := range results {
			log.Info("Result", "config", r.Config, "mode", r.Mode, "throughputMbps", r.Summary.ThroughputMbps, "durationMs", r.Summary.DurationMs, "failures", r.Summary.TotalIndividualFailures)
		}
		log.Info(strings.Repeat("=", 80))
	})
})

// runPerfClientWithConfig runs the perf client job with the given config and waits for completion.
// Returns the parsed perfsummary.Summary from the job logs.
func runPerfClientWithConfig(ctx context.Context, config *perfclient.Config) *perfsummary.Summary {
	config.WithNamespace(namespace).
		WithServerHostname(buildServerHostname(namespace, *cacheServerCount)).
		WithDataSizeGiB(*dataSizeGiB).
		WithNumThreads(*numThreads).
		WithChecksumAlgorithm(perfclient.ChecksumAlgorithm(*checksumAlgorithm))

	By(fmt.Sprintf("Creating %s perf client job (checksum calc=%t, validate=%t)",
		config.Mode, config.EnableCalculateChecksum, config.EnableValidateChecksum))
	job, err := config.LoadJob()
	Expect(err).NotTo(HaveOccurred(), "Failed to load perf client job")

	// Delete any existing job first
	deleteJob(ctx, job.Name, namespace)

	// Apply the job using kubectl
	jobYAML, err := config.JobYAML()
	Expect(err).NotTo(HaveOccurred(), "Failed to generate job YAML")

	cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
	cmd.Stdin = utils.NewStringReader(string(jobYAML))
	output, err := utils.Run(cmd)
	Expect(err).NotTo(HaveOccurred(), "Failed to apply perf client job: %s", output)

	By(fmt.Sprintf("Waiting for %s job to complete", config.Mode))
	waitForJobCompletion(ctx, job.Name, namespace, 5*time.Minute)

	By(fmt.Sprintf("Parsing %s job logs", config.Mode))
	summary, err := getJobPerfSummary(ctx, job.Name, namespace)
	Expect(err).NotTo(HaveOccurred(), "Failed to get perf summary from job logs")

	// Log and optionally send metrics to Kusto via EventHub
	By("Logging performance metrics")
	helmInfo := helm.GetReleaseInfo(ctx, namespace, "cache-server")
	gitCommit := git.GetCommit(ctx)
	gitBranch := git.GetBranch(ctx)
	clusterName := getClusterName()
	podName, nodeName, nodeSKU := getJobPodInfo(ctx, job.Name, namespace)
	specReport := CurrentSpecReport()
	metric := &kusto.PerfMetric{
		SuiteID:          suiteUUID,
		TestName:         specReport.FullText(),
		TestGitCommit:    gitCommit,
		TestGitBranch:    gitBranch,
		HelmChartVersion: helmInfo.ChartVersion,
		HelmAppVersion:   helmInfo.AppVersion,
		TestMetadata: kusto.TestMetadata{
			ClusterName: clusterName,
			PodName:     podName,
			NodeName:    nodeName,
			NodeSKU:     nodeSKU,
			IsMinikube:  cluster.IsMinikube(),
		},
		Result: kusto.PerfResult{
			Mode:                    summary.Mode,
			DurationMs:              summary.DurationMs,
			TotalBytesTransferred:   summary.TotalBytesTransferred,
			ThroughputMbps:          summary.ThroughputMbps,
			TotalFailures:           summary.TotalIndividualFailures,
			ExpectedChunks:          summary.ExpectedChunksToTransfer,
			SuccessfulChunks:        summary.ChunksSuccessfullyTransferred,
			NumThreads:              config.NumThreads,
			DataSizeGiB:             config.DataSizeGiB,
			NumIterations:           config.NumIterations,
			NumServers:              *cacheServerCount,
			EnableCalculateChecksum: config.EnableCalculateChecksum,
			EnableValidateChecksum:  config.EnableValidateChecksum,
			ChecksumAlgorithm:       string(config.ChecksumAlgorithm),
		},
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

// getClusterName gets the current kubectl context name.
func getClusterName() string {
	context, err := cluster.GetCurrentContext()
	if err != nil {
		log.Error(err, "Failed to get kubectl context")
		return "<unknown>"
	}
	return context
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

// deleteJob deletes the job if it exists.
func deleteJob(ctx context.Context, name, ns string) {
	cmd := exec.CommandContext(ctx, "kubectl", "delete", "job", name,
		"-n", ns, "--ignore-not-found=true")
	_, _ = utils.Run(cmd)
}

// getJobPodInfo returns the pod name, node name, and node SKU for a job's pod.
func getJobPodInfo(ctx context.Context, jobName, ns string) (podName, nodeName, nodeSKU string) {
	// Get pod name from job using the app label (job name) and test-pod marker
	podCmd := exec.CommandContext(ctx, "kubectl", "get", "pods",
		"-n", ns,
		"-l", fmt.Sprintf("app=%s,dacs.azure.com/test-pod=true", jobName),
		"-o", "jsonpath={.items[0].metadata.name}")
	podOutput, err := utils.RunOutput(podCmd)
	if err != nil {
		log.Error(err, "Failed to get pod name for job", "job", jobName)
		return "<unknown>", "<unknown>", ""
	}
	podName = strings.TrimSpace(podOutput)
	if podName == "" {
		log.Info("No pod found for job", "job", jobName)
		return "<unknown>", "<unknown>", ""
	}

	// Get node name from pod
	nodeCmd := exec.CommandContext(ctx, "kubectl", "get", "pod", podName,
		"-n", ns,
		"-o", "jsonpath={.spec.nodeName}")
	nodeOutput, err := utils.RunOutput(nodeCmd)
	if err != nil {
		log.Error(err, "Failed to get node name for pod", "pod", podName)
		return podName, "<unknown>", ""
	}
	nodeName = strings.TrimSpace(nodeOutput)

	// Get node SKU from node labels (AKS sets node.kubernetes.io/instance-type)
	skuCmd := exec.CommandContext(ctx, "kubectl", "get", "node", nodeName,
		"-o", "jsonpath={.metadata.labels.node\\.kubernetes\\.io/instance-type}")
	skuOutput, err := utils.RunOutput(skuCmd)
	if err != nil {
		log.Error(err, "Failed to get node SKU", "node", nodeName)
		return podName, nodeName, ""
	}
	nodeSKU = strings.TrimSpace(skuOutput)

	return podName, nodeName, nodeSKU
}

// waitForJobCompletion waits for the job to complete or fail.
func waitForJobCompletion(ctx context.Context, name, ns string, timeout time.Duration) {
	Eventually(func(g Gomega) {
		// Check if job failed first
		failedCmd := exec.CommandContext(ctx, "kubectl", "get", "job", name,
			"-n", ns,
			"-o", "jsonpath={.status.conditions[?(@.type==\"Failed\")].status}")
		failedOutput, _ := utils.RunOutput(failedCmd)
		g.Expect(failedOutput).NotTo(Equal("True"), "Job failed")

		// Check if job completed
		cmd := exec.CommandContext(ctx, "kubectl", "get", "job", name,
			"-n", ns,
			"-o", "jsonpath={.status.conditions[?(@.type==\"Complete\")].status}")
		output, err := utils.RunOutput(cmd)
		g.Expect(err).NotTo(HaveOccurred(), "Failed to get job status")
		g.Expect(output).To(Equal("True"), "Job not yet complete")
	}).WithTimeout(timeout).WithPolling(5*time.Second).Should(Succeed(),
		fmt.Sprintf("Job %s did not complete within %v", name, timeout))
}

// getJobPerfSummary retrieves job logs and parses the performance summary.
func getJobPerfSummary(ctx context.Context, name, ns string) (*perfsummary.Summary, error) {
	cmd := exec.CommandContext(ctx, "kubectl", "logs",
		fmt.Sprintf("job/%s", name),
		"-n", ns)
	output, err := utils.Run(cmd)
	if err != nil {
		return nil, fmt.Errorf("failed to get job logs: %w", err)
	}

	summary, err := perfsummary.Parse(output)
	if err != nil {
		return nil, fmt.Errorf("failed to parse perf summary from logs: %w", err)
	}

	log.Info("Performance Summary", "job", name, "summary", summary.String())

	return summary, nil
}
