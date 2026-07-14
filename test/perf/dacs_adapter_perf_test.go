// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

package perf

import (
	"context"
	"fmt"
	"os/exec"
	"strconv"
	"strings"
	"time"

	. "github.com/onsi/ginkgo/v2"
	batchv1 "k8s.io/api/batch/v1"
	corev1 "k8s.io/api/core/v1"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/yaml"

	"github.com/Azure/dacs/test/pkg/cluster"
	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/common/fixtures"
	"github.com/Azure/dacs/test/pkg/git"
	"github.com/Azure/dacs/test/pkg/helm"
	"github.com/Azure/dacs/test/pkg/kube/job"
	"github.com/Azure/dacs/test/pkg/kube/pod"
	"github.com/Azure/dacs/test/pkg/kusto"
	"github.com/Azure/dacs/test/pkg/perfsummary"
	"github.com/Azure/dacs/test/pkg/utils"
)

const (
	adapterBenchmarkResultPath = "/tmp/results.json"
)

// AdapterBenchmarkConfig holds configuration for the adapter benchmark job.
type AdapterBenchmarkConfig struct {
	k8sClient                client.Client
	workloadIdentityClientID string
	serviceAccountName       string
	cacheServerNamespace     string
	storageAccountName       string
	containerName            string
	testImage                string
	clients                  string // comma-separated: blobfile,dacs,dacs_zero_copy
	blobSizeGB               float64
	numBlobs                 int
	numProcesses             int
	writeMode                string
	cacheDiscoveryURL        string
	consistentMode           bool
}

func (config *AdapterBenchmarkConfig) runBenchmark(ctx context.Context) (*perfsummary.DACSAdapterBenchmarkSummary, error) {
	jobData, err := fixtures.ReadDACSAdapterBenchmarkJob()
	if err != nil {
		return nil, fmt.Errorf("failed to read benchmark job fixture: %w", err)
	}

	var j batchv1.Job
	if err := yaml.Unmarshal(jobData, &j); err != nil {
		return nil, fmt.Errorf("failed to unmarshal job fixture: %w", err)
	}

	jobConfig := job.NewJobConfig(&j).
		WithName("dacs-adapter-benchmark").
		WithNamespace(config.cacheServerNamespace).
		WithParallelism(1).
		WithCompletions(1).
		WithMatchLabels(map[string]string{"app": "dacs-adapter-benchmark"}).
		WithRestartPolicy(corev1.RestartPolicyNever).
		WithImage(config.testImage).
		WithEnv(0, "AZURE_STORAGE_ACCOUNT_NAME", config.storageAccountName).
		WithEnv(0, "CONTAINER_NAME", config.containerName).
		WithEnv(0, "CACHE_DISCOVERY_URL", config.cacheDiscoveryURL).
		WithEnv(0, "CACHE_SERVER_PORT", "9065").
		WithEnv(0, "BENCHMARK_CLIENTS", config.clients).
		WithEnv(0, "BLOB_SIZE_GB", strconv.FormatFloat(config.blobSizeGB, 'f', 1, 64)).
		WithEnv(0, "NUM_BLOBS", strconv.Itoa(config.numBlobs)).
		WithEnv(0, "NUM_PROCESSES", strconv.Itoa(config.numProcesses)).
		WithEnv(0, "WRITE_MODE", config.writeMode).
		WithEnvDownwardAPIPodName()

	if config.consistentMode {
		jobConfig.WithEnv(0, "CONSISTENT_MODE", "true")
	}

	// Configure credentials: workload identity > SAS token > managed identity
	if config.workloadIdentityClientID != "" && config.serviceAccountName != "" {
		By(fmt.Sprintf("Using WorkloadIdentity ClientID: %s for authentication", config.workloadIdentityClientID))
		saYAML, err := common.WorkloadIdentitySAYAML(config.serviceAccountName, config.cacheServerNamespace, config.workloadIdentityClientID)
		if err != nil {
			return nil, fmt.Errorf("failed to create workload identity SA YAML: %w", err)
		}
		cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
		cmd.Stdin = utils.NewStringReader(string(saYAML))
		if output, err := utils.Run(cmd); err != nil {
			return nil, fmt.Errorf("failed to apply workload identity service account: %s: %w", output, err)
		}
		jobConfig.WithServiceAccountName(config.serviceAccountName).
			WithWorkloadIdentityLabel()
	} else {
		accessEnvKey, accessEnvValue, err := common.GetStorageAccessEnv(ctx, config.k8sClient, config.storageAccountName)
		if err != nil {
			return nil, fmt.Errorf("failed to get storage access env: %w", err)
		}
		if accessEnvKey == common.CACHECLIENT_SAS_TOKEN_KEY {
			s, err := common.CreateSASTokenSecret(ctx, config.k8sClient, "adapter-benchmark-secret", config.cacheServerNamespace, accessEnvValue)
			if err != nil {
				return nil, err
			}
			jobConfig.WithEnvFromSecret(0, s)
		} else {
			jobConfig.WithEnv(0, common.CACHECLIENT_CLIENT_ID_KEY, accessEnvValue)
		}
	}

	j = *jobConfig.Job

	if err := job.Delete(ctx, j.Name, j.Namespace); err != nil {
		return nil, fmt.Errorf("failed to delete existing benchmark job: %w", err)
	}
	time.Sleep(2 * time.Second)

	if err := config.k8sClient.Create(ctx, &j); err != nil {
		return nil, fmt.Errorf("failed to create benchmark job: %w", err)
	}

	log.Info("Waiting for benchmark job to complete", "job", j.Name)
	job.WaitForCompletion(ctx, j.Name, j.Namespace, 90*time.Minute)

	var podList corev1.PodList
	if err := config.k8sClient.List(ctx, &podList,
		client.InNamespace(j.Namespace),
		client.MatchingLabels(j.Spec.Template.Labels)); err != nil {
		return nil, fmt.Errorf("failed to list job pods: %w", err)
	}

	if len(podList.Items) == 0 {
		return nil, fmt.Errorf("no pods found for job %s", j.Name)
	}

	p := podList.Items[0]
	logs, err := pod.GetLogs(ctx, p.Name, p.Namespace)
	if err != nil {
		return nil, fmt.Errorf("failed to get pod logs: %w", err)
	}

	if !pod.IsPodExitedSuccessfully(ctx, &p) {
		return nil, fmt.Errorf("benchmark pod failed. Logs: %s", logs)
	}

	// Extract JSON results from logs (between markers)
	jsonData := extractJSONFromLogs(logs)
	if jsonData == "" {
		return nil, fmt.Errorf("failed to extract results JSON from logs. Logs: %s", logs)
	}

	return perfsummary.ParseDACSAdapterBenchmark(jsonData)
}

// extractJSONFromLogs extracts the JSON results from pod logs between markers.
func extractJSONFromLogs(logs string) string {
	const startMarker = "=== BENCHMARK_RESULTS_JSON ==="
	const endMarker = "=== END_BENCHMARK_RESULTS_JSON ==="

	startIdx := strings.Index(logs, startMarker)
	if startIdx == -1 {
		return ""
	}
	startIdx += len(startMarker)

	endIdx := strings.Index(logs[startIdx:], endMarker)
	if endIdx == -1 {
		return ""
	}

	return strings.TrimSpace(logs[startIdx : startIdx+endIdx])
}

func sendAdapterBenchmarkMetrics(ctx context.Context, result *perfsummary.DACSAdapterBenchmarkSummary) {
	helmInfo := helm.GetCacheReleaseInfo(ctx, namespace)
	gitCommit := git.GetCommit(ctx)
	gitBranch := git.GetBranch(ctx)
	clusterName, _ := cluster.GetCurrentContext()
	specReport := CurrentSpecReport()

	for clientName, r := range result.Results {
		writeMetric := &kusto.PerfMetric{
			SuiteID:          suiteUUID,
			TestName:         fmt.Sprintf("%s/adapter-benchmark/%s/write", specReport.FullText(), clientName),
			TestGitCommit:    gitCommit,
			TestGitBranch:    gitBranch,
			HelmChartVersion: helmInfo.ChartVersion,
			HelmAppVersion:   helmInfo.AppVersion,
			TestMetadata: kusto.TestMetadata{
				ClusterName: clusterName,
				IsMinikube:  cluster.IsMinikube(),
			},
			Result: kusto.AdapterPerfResult{
				ClientName:            fmt.Sprintf("%s-write", clientName),
				BlobSizeGB:            result.Config.BlobSizeGB,
				NumBlobs:              result.Config.NumBlobs,
				NumProcesses:          result.Config.NumProcesses,
				WriteMode:             result.Config.WriteMode,
				TotalBytesTransferred: r.WriteBytesTransferred,
				DurationSeconds:       r.WriteDurationSeconds,
				ThroughputMbps:        r.WriteThroughputMBps * 8, // Convert MBps to Mbps
			},
		}
		if err := kusto.SendPerfMetric(ctx, kusto.DefaultEventHubConfig, writeMetric); err != nil {
			log.Error(err, "Failed to send write metric", "client", clientName)
		}

		readMetric := &kusto.PerfMetric{
			SuiteID:          suiteUUID,
			TestName:         fmt.Sprintf("%s/adapter-benchmark/%s/read", specReport.FullText(), clientName),
			TestGitCommit:    gitCommit,
			TestGitBranch:    gitBranch,
			HelmChartVersion: helmInfo.ChartVersion,
			HelmAppVersion:   helmInfo.AppVersion,
			TestMetadata: kusto.TestMetadata{
				ClusterName: clusterName,
				IsMinikube:  cluster.IsMinikube(),
			},
			Result: kusto.AdapterPerfResult{
				ClientName:            fmt.Sprintf("%s-read", clientName),
				BlobSizeGB:            result.Config.BlobSizeGB,
				NumBlobs:              result.Config.NumBlobs,
				NumProcesses:          result.Config.NumProcesses,
				WriteMode:             result.Config.WriteMode,
				TotalBytesTransferred: r.ReadBytesTransferred,
				DurationSeconds:       r.ReadDurationSeconds,
				ThroughputMbps:        r.ReadThroughputMBps * 8, // Convert MBps to Mbps
			},
		}
		if err := kusto.SendPerfMetric(ctx, kusto.DefaultEventHubConfig, readMetric); err != nil {
			log.Error(err, "Failed to send read metric", "client", clientName)
		}

		// Send overwrite metric if overwrite phase was run
		if r.OverwriteBytesTransferred > 0 {
			overwriteMetric := &kusto.PerfMetric{
				SuiteID:          suiteUUID,
				TestName:         fmt.Sprintf("%s/adapter-benchmark/%s/overwrite", specReport.FullText(), clientName),
				TestGitCommit:    gitCommit,
				TestGitBranch:    gitBranch,
				HelmChartVersion: helmInfo.ChartVersion,
				HelmAppVersion:   helmInfo.AppVersion,
				TestMetadata: kusto.TestMetadata{
					ClusterName: clusterName,
					IsMinikube:  cluster.IsMinikube(),
				},
				Result: kusto.AdapterPerfResult{
					ClientName:            fmt.Sprintf("%s-overwrite", clientName),
					BlobSizeGB:            result.Config.BlobSizeGB,
					NumBlobs:              result.Config.NumBlobs,
					NumProcesses:          result.Config.NumProcesses,
					WriteMode:             result.Config.WriteMode,
					TotalBytesTransferred: r.OverwriteBytesTransferred,
					DurationSeconds:       r.OverwriteDurationSeconds,
					ThroughputMbps:        r.OverwriteThroughputMBps * 8,
				},
			}
			if err := kusto.SendPerfMetric(ctx, kusto.DefaultEventHubConfig, overwriteMetric); err != nil {
				log.Error(err, "Failed to send overwrite metric", "client", clientName)
			}
		}
	}
}
