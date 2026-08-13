/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# End to End test for dacs cache clients performing writes
# --------------------------------------------------------------------------------------------------------------------
*/
package perf

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/Azure/dacs/test/pkg/cluster"
	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/common/fixtures"
	"github.com/Azure/dacs/test/pkg/git"
	"github.com/Azure/dacs/test/pkg/helm"
	"github.com/Azure/dacs/test/pkg/kube/mpi"
	"github.com/Azure/dacs/test/pkg/kube/node"
	"github.com/Azure/dacs/test/pkg/kusto"
	perf "github.com/Azure/dacs/test/pkg/perfsummary"
	"github.com/Azure/dacs/test/pkg/utils"
	"github.com/kubeflow/mpi-operator/pkg/apis/kubeflow/v2beta1"
	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	corev1 "k8s.io/api/core/v1"
	"sigs.k8s.io/controller-runtime/pkg/client"
)

var (
	// Note: To resolve authentication issue in pipeline, test must run in cache-server namespace
	cacheClientNs       = "cache-server"
	containerNamePrefix = "dacs-perf-"
	defaultWaitTime     = 2 * time.Minute
	// Install latest version
	mpiOperatorPath = "https://raw.githubusercontent.com/kubeflow/mpi-operator/master/deploy/v2beta1/mpi-operator.yaml"
)

// CacheClient holds information required to deploy cacheclient
type CacheClientConfig struct {
	k8sClient                client.Client // Kubernetes client to interact with cluster
	workloadIdentityClientID string        // Client ID of the Azure managed identity to be used for workload identity auth (if applicable)
	serviceAccountName       string        // Kubernetes ServiceAccount name to be used for workload identity (if applicable)

	count                int    // Number of dacs clients to deploy
	cacheServerNamespace string // Namespace where cacheservers are deployed
	cacheClientImage     string // Cache client image to use
	storageAccountName   string // Azure Storage Account name
	containerName        string // Azure Storage Account container name
	blobCount            uint   // Number of blobs to write/read per client
	blobSize             uint64 // Size of each blob in bytes
	writeMode            string // Write mode to use for cache client
}

func (config *CacheClientConfig) dacsClientTestSuite(ctx context.Context) {
	testName := fmt.Sprintf("Should create and run %d cache clients successfully", config.count)

	// TODO: Re-enable dynamic container creation after fixing az storage CLI access from pipeline agents.
	// err := utils.CreateContainerIfNotExists(config.storageAccountName, config.containerName)
	// Expect(err).ShouldNot(HaveOccurred(), "Failed to check/create container in storage account")

	config.dacsClientTest(ctx, testName)

	// TODO: Re-enable container cleanup after fixing az storage CLI access from pipeline agents.
	// err = utils.DeleteContainer(config.storageAccountName, config.containerName)
	// Expect(err).ShouldNot(HaveOccurred(), "Failed to delete container in storage account")
}

func (config *CacheClientConfig) dacsClientTest(ctx context.Context, testName string) {
	mpiJobName := fmt.Sprintf("dacs-client-mpi-%d", config.count)
	// Read MPI job fixture and modify accordingly
	mpiJob, err := mpi.LoadMPIJob(fixtures.ReadMPIJob)
	Expect(err).ShouldNot(HaveOccurred(), "Failed to load MPI job fixture")

	cacheDiscoveryURL := common.GetDiscoveryURLForCache(cacheName, config.cacheServerNamespace)

	accessEnvKey, accessEnvValue, err := common.GetStorageAccessEnv(ctx, config.k8sClient, config.storageAccountName)
	Expect(err).ShouldNot(HaveOccurred(), "Failed to get storage access key/value for cacheclient job")

	// Wait for any existing mpijob with the same name to be deleted before creating a new one
	By("Waiting for any existing mpijob with the same name to be deleted")
	_, err = utils.Run(exec.CommandContext(ctx, "kubectl", "delete", "mpijob", mpiJobName, "-n", cacheClientNs))
	if err != nil && !strings.Contains(err.Error(), "NotFound") {
		log.Error(err, "Error while deleting existing mpijob %s/%s (non-fatal)", cacheClientNs, mpiJobName)
	}
	Eventually(func(g Gomega) {
		By(fmt.Sprintf("Checking if the existing mpijob %s/%s has been deleted", cacheClientNs, mpiJobName))
		cmd := exec.CommandContext(ctx, "kubectl", "get", "mpijob", mpiJobName, "-n", cacheClientNs)
		output, err := utils.Run(cmd)
		if err != nil {
			// "NotFound" error means the job is already deleted — which is the desired state
			g.Expect(strings.Contains(output, "NotFound")).To(BeTrue(),
				"Unexpected error while checking mpijob %s/%s: %s", cacheClientNs, mpiJobName, output)
			return
		}
		// If no error, the job still exists — fail so Eventually retries
		g.Expect(output).To(BeEmpty(), "Expected mpijob %s/%s to be deleted", cacheClientNs, mpiJobName)
	}).WithContext(ctx).WithTimeout(defaultWaitTime).WithPolling(5 * time.Second).Should(Succeed())

	// If workload identity is configured, ensure the ServiceAccount exists
	if saYAML, err := common.WorkloadIdentitySAYAML(config.serviceAccountName, cacheClientNs, config.workloadIdentityClientID); err == nil && saYAML != nil {
		By("Ensuring workload identity ServiceAccount exists")
		cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
		cmd.Stdin = utils.NewStringReader(string(saYAML))
		output, err := utils.Run(cmd)
		Expect(err).NotTo(HaveOccurred(), "Failed to apply ServiceAccount: %s", output)
	}

	// Overwrite only required fields in the MPIJob fixture
	mpiJob = mpiJob.WithName(mpiJobName).
		WithNamespace(cacheClientNs).
		WithCleanPodPolicy(v2beta1.CleanPodPolicyNone).
		WithLauncherImage(config.cacheClientImage, corev1.PullIfNotPresent).
		WithWorkerImage(config.cacheClientImage, corev1.PullIfNotPresent).
		WithWorkerReplicas(int32(config.count)).
		WithLauncherEnv(corev1.EnvVar{Name: "CACHE_DISCOVERY_URL", Value: cacheDiscoveryURL}).
		WithLauncherEnv(corev1.EnvVar{Name: "AZURE_STORAGE_ACCOUNT_NAME", Value: config.storageAccountName}).
		WithLauncherCommand([]string{"/bin/bash", "-lc"}...).
		WithWorkerPodAntiAffinityForJob().
		WithLauncherArgs(config.getLauncherCommandArgs()...)

	// Note: WorkloadIdentityClientID will take higher precedence
	if config.workloadIdentityClientID != "" && config.serviceAccountName != "" {
		By(fmt.Sprintf("Using WorkloadIdentity ClientID: %s for authentication", config.workloadIdentityClientID))
		mpiJob = mpiJob.WithServiceAccountName(config.serviceAccountName).
			WithWorkloadIdentityLabel()
	} else if accessEnvKey == common.CACHECLIENT_SAS_TOKEN_KEY {
		// Create Secret with SAS token in the job namespace
		By("Using SAS_TOKEN for client authentication")
		_, err := common.CreateSASTokenSecret(ctx, config.k8sClient, "cacheclient-secret", mpiJob.Namespace, accessEnvValue)
		Expect(err).ShouldNot(HaveOccurred(), "Failed to create secret for cacheclient job")
		mpiJob = mpiJob.WithLauncherEnv(corev1.EnvVar{Name: common.CACHECLIENT_SAS_TOKEN_KEY, Value: accessEnvValue})
	} else {
		By(fmt.Sprintf("Using AKS Node Pool Managed identity %s for authentication", accessEnvValue))
		clusterName, err := cluster.GetCurrentContext()
		Expect(err).ShouldNot(HaveOccurred(), "Failed to get cluster name")

		rg, err := utils.GetResourceGroupOfCluster(clusterName)
		Expect(err).ShouldNot(HaveOccurred(), "Failed to get resource group")

		cmd := utils.GetRoleAssignmentCommand(config.storageAccountName, clusterName, rg)
		roleErrorMsg := fmt.Sprintf(
			"Ensure to grant 'Storage Blob Data Contributor' role to the managed identity %s. By running: %s",
			accessEnvValue,
			cmd,
		)
		// Using Kubelet Identity for storageaccount access
		// Note: If access is granted recently then role assignment propagation might take some time
		Eventually(func() bool {
			By(fmt.Sprintf("Checking if managed identity with clientId %s has required role assignment on storage account %s", accessEnvValue, config.storageAccountName))
			isRoleAlreadyExist, err := utils.CheckRoleAssignments(accessEnvValue, config.storageAccountName, "Storage Blob Data Contributor")
			if err != nil {
				log.Error(err, "Error while checking role assignment")
				return false
			}
			return isRoleAlreadyExist
		}).WithContext(ctx).WithTimeout(defaultWaitTime).Should(BeTrue(), roleErrorMsg)
		mpiJob = mpiJob.WithLauncherEnv(corev1.EnvVar{Name: common.CACHECLIENT_CLIENT_ID_KEY, Value: accessEnvValue})
	}

	mpiJobYAML, err := mpiJob.YAML()
	Expect(err).ShouldNot(HaveOccurred())
	cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
	cmd.Stdin = utils.NewStringReader(string(mpiJobYAML))
	output, err := utils.Run(cmd)
	Expect(err).ShouldNot(HaveOccurred(), "Failed to apply cacheclient MPI job: %s", output)

	// Wait for the job to complete
	By(fmt.Sprintf("Waiting for %s job to complete", mpiJob.ObjectMeta.Name))
	mpi.WaitForCompletion(ctx, mpiJob.ObjectMeta.Name, mpiJob.ObjectMeta.Namespace, 15*time.Minute)

	isJobSucceeded, err := mpi.IsJobSucceeded(ctx, mpiJob.ObjectMeta.Name, mpiJob.ObjectMeta.Namespace)
	Expect(err).ShouldNot(HaveOccurred(), "Failed to check if mpijob succeeded")
	Expect(isJobSucceeded).Should(BeTrue(), "Cacheclient MPI job did not complete successfully")

	// Since launcher pod does exec into workers and runs the test command, we can get the aggregated test results from launcher pod logs
	var launcherPodList corev1.PodList
	err = config.k8sClient.List(ctx, &launcherPodList, client.InNamespace(mpiJob.ObjectMeta.Namespace), client.MatchingLabels(mpiJob.GetLauncherPodLabels()))
	Expect(err).ShouldNot(HaveOccurred(), "Failed to list pods for cacheclient job")

	var launcherPod *corev1.Pod
	// Note: It was observed that dns resolution to worker may fail in pipeline which causes the new launcher pod to run
	// Pick only the pod which was completed
	for i := range launcherPodList.Items {
		if launcherPodList.Items[i].Status.Phase == corev1.PodSucceeded {
			launcherPod = &launcherPodList.Items[i]
			break
		}
	}

	// List the worker pods to map node name to test result for each client
	var workerPodList corev1.PodList
	err = config.k8sClient.List(ctx, &workerPodList, client.InNamespace(mpiJob.ObjectMeta.Namespace), client.MatchingLabels(mpiJob.GetWorkerPodLabels()))
	Expect(err).ShouldNot(HaveOccurred(), "Failed to list pods for cacheclient job")
	Expect(config.count).Should(Equal(len(workerPodList.Items)), "Expected exactly %d worker pods, but found %d", config.count, len(workerPodList.Items))

	// Collect the logs of launcher pod which has the aggregated performance metrics from all clients and parse the results
	Expect(launcherPod).ShouldNot(BeNil(), "Failed to find the launcher pod which completed successfully")
	err = collectLogsFromLauncherPod(ctx, launcherPod.Name, launcherPod.Namespace)
	Expect(err).ShouldNot(HaveOccurred(), "Failed to collect logs from launcher pod %s/%s", launcherPod.Namespace, launcherPod.Name)

	podsMetadata := make([]kusto.PodMetadata, len(workerPodList.Items))
	var perfSummary perf.DACSClientPerfSummary
	for i, pod := range workerPodList.Items {

		By(fmt.Sprintf("Collecting test result of Pod: %d %s/%s through launcher pod %s/%s logs", i, pod.Namespace, pod.Name, launcherPod.Namespace, launcherPod.Name))

		// Extract Index from worker pod name
		indexStr := strings.Split(pod.Name, "-")[len(strings.Split(pod.Name, "-"))-1]
		podIndex, err := strconv.Atoi(indexStr)
		Expect(err).ShouldNot(HaveOccurred(), fmt.Sprintf("Failed to extract index from worker pod name %s", pod.Name))

		perfMetrics, err := getPerformanceMetricsFromLogs(podIndex)
		Expect(err).ShouldNot(HaveOccurred(), fmt.Sprintf("Failed to get performance metrics of pod %s/%s", pod.Namespace, pod.Name))

		podsMetadata[i] = kusto.PodMetadata{
			PodName:  pod.Name,
			NodeName: pod.Spec.NodeName,
			NodeSKU:  node.GetSKU(ctx, pod.Spec.NodeName),
			PerfSummary: kusto.DACSClientPerfResult{
				TotalBlobsWritten:     perfMetrics.TotalBlobsWritten,
				TotalBytesWritten:     perfMetrics.TotalBytesWritten,
				TotalWriteTimeSeconds: perfMetrics.TotalWriteTimeSeconds,
				WriteThroughputGbps:   perfMetrics.WriteThroughputGbps,
				TotalBytesRead:        perfMetrics.TotalBytesRead,
				TotalReadTimeSeconds:  perfMetrics.TotalReadTimeSeconds,
				ReadThroughputGbps:    perfMetrics.ReadThroughputGbps,
			},
		}
		perfSummary.TotalBlobsWritten += perfMetrics.TotalBlobsWritten
		perfSummary.TotalBytesWritten += perfMetrics.TotalBytesWritten
		perfSummary.TotalWriteTimeSeconds += perfMetrics.TotalWriteTimeSeconds
		perfSummary.WriteThroughputGbps += perfMetrics.WriteThroughputGbps

		perfSummary.TotalBytesRead += perfMetrics.TotalBytesRead
		perfSummary.TotalReadTimeSeconds += perfMetrics.TotalReadTimeSeconds
		perfSummary.ReadThroughputGbps += perfMetrics.ReadThroughputGbps
	}
	By(fmt.Sprintf("Aggregated Cache Client Performance Summary: %s", perfSummary.String()))

	// Log and optionally send metrics to Kusto via EventHub
	By("Logging performance metrics")
	helmInfo := helm.GetCacheReleaseInfo(ctx, config.cacheServerNamespace)
	gitCommit := git.GetCommit(ctx)
	gitBranch := git.GetBranch(ctx)
	clusterName, _ := cluster.GetCurrentContext()
	specReport := CurrentSpecReport()
	metric := &kusto.PerfMetric{
		SuiteID:          suiteUUID,
		Timestamp:        time.Now().Format(time.RFC3339),
		TestName:         specReport.FullText(),
		TestGitCommit:    gitCommit,
		TestGitBranch:    gitBranch,
		HelmChartVersion: helmInfo.ChartVersion,
		HelmAppVersion:   helmInfo.AppVersion,
		TestMetadata: kusto.DACSClientMetadata{
			ClusterName:    clusterName,
			StorageAccount: config.storageAccountName,
			ContainerName:  config.containerName,
			JobName:        mpiJob.Name,
			Clients:        config.count,
			PodsMetadata:   podsMetadata,
			IsMinikube:     cluster.IsMinikube(),
			WriteMode:      config.writeMode,
		},
		Result: kusto.DACSClientPerfResult{
			TotalBlobsWritten:     perfSummary.TotalBlobsWritten,
			TotalBytesWritten:     perfSummary.TotalBytesWritten,
			TotalWriteTimeSeconds: perfSummary.TotalWriteTimeSeconds,
			WriteThroughputGbps:   perfSummary.WriteThroughputGbps,
			TotalBytesRead:        perfSummary.TotalBytesRead,
			TotalReadTimeSeconds:  perfSummary.TotalReadTimeSeconds,
			ReadThroughputGbps:    perfSummary.ReadThroughputGbps,
		},
	}
	// SendPerfMetric always prints the metric; only sends to EventHub if SEND_METRICS=true
	err = kusto.SendPerfMetric(ctx, kusto.DefaultEventHubConfig, metric)
	if err != nil {
		log.Error(err, "Failed to send perf metric (non-fatal)")
	}

	// NOTE: No deletion of the mpijob required since
	// ttlSecondsAfterFinished is set to 300, to allow
	// log collection from support bundle.
}

// Store the logs on temp directory with filename format: <pod-name>_logs.txt
func collectLogsFromLauncherPod(ctx context.Context, podName, namespace string) error {
	// Capture logs directly and write to file; avoid shell redirection
	cmd := exec.CommandContext(ctx, "kubectl", "logs", podName, "-n", namespace)
	logs, err := utils.Run(cmd)
	if err != nil {
		return err
	}
	// If SupportBundle directory doesn't exist, create it
	if _, err := os.Stat(opts.SupportBundleDir); os.IsNotExist(err) {
		log.Info("Support bundle directory does not exist, creating it: %s", opts.SupportBundleDir)
		err = os.MkdirAll(opts.SupportBundleDir, 0755)
		if err != nil {
			return fmt.Errorf("failed to create support bundle directory: %v", err)
		}
	}
	outPath := fmt.Sprintf("%s/%s_logs.txt", opts.SupportBundleDir, podName)
	err = os.WriteFile(outPath, []byte(logs), 0644)
	if err != nil {
		return err
	}

	// Collected logs will look like below format:
	/*
	   [1,0]<stdout>:DACS_CTR:WARN|CacheClient|time_int:1770614852712707521|pid:57|tid:76|operation_Id:|rank:-1|CacheClientImpl.cpp:operator()(391): reqid:134b3627-59ae5344-4863b053-76e5046b| ...
	   [1,1]<stdout>:DACS_CTR:INFO|StorageCommon|time_int:1770614852646534453|pid:69|tid:69|operation_Id:|rank:-1|ConnectionManager.cpp:~ConnectionManager(187): Purge all open connections
	   [1,2]<stdout>:DACS_CTR:WARN|StorageCommon|time_int:1770614852736524792|pid:45|tid:89|operation_Id:|rank:-1|SocketConnection.cpp:TimedWrite(157): ...
	   [1,3]<stdout>:DACS_CTR:WARN|CacheClient|time_int:1770614852893491893|pid:69|tid:96|operation_Id:|rank:-1|CacheClientImpl.cpp:operator()(330): ...
	*/
	lines := strings.Split(logs, "\n")
	out := make(map[int][]string)
	for _, line := range lines {
		// Expect prefix like [1,3]
		if len(line) < 5 || line[0] != '[' {
			continue
		}
		// Find comma and closing bracket
		comma := strings.IndexByte(line, ',')
		end := strings.IndexByte(line, ']')
		if comma <= 1 || end <= comma+1 {
			continue
		}
		// Parse worker index between comma+1 and ]
		idxStr := line[comma+1 : end]
		podIdx, err := strconv.Atoi(idxStr)
		if err != nil {
			continue
		}
		out[podIdx] = append(out[podIdx], line)
	}
	// Join grouped lines
	for k, v := range out {
		err := os.WriteFile(fmt.Sprintf("%s/worker-%d", opts.SupportBundleDir, k), []byte(strings.Join(v, "\n")), 0644)
		if err != nil {
			return err
		}
	}
	return nil
}

// getPerformanceMetricsFromLogs parses metrics from a provided logs string for a specific worker index
func getPerformanceMetricsFromLogs(workerPodIndex int) (*perf.DACSClientPerfSummary, error) {
	logsBytes, err := os.ReadFile(fmt.Sprintf("%s/worker-%d", opts.SupportBundleDir, workerPodIndex))
	if err != nil {
		return nil, fmt.Errorf("failed to read logs for worker index %d: %v", workerPodIndex, err)
	}
	logs := string(logsBytes)
	if strings.TrimSpace(logs) == "" {
		return nil, fmt.Errorf("empty logs for worker index %d", workerPodIndex)
	}
	writePerfRegx := regexp.MustCompile(fmt.Sprintf("(?m)^\\[%d,%d\\]<stdout>:.*?DACS_Cache_Client_Test:Wrote (\\d+) blobs with total (\\d+) bytes .* in (\\d+(?:\\.\\d+)?) seconds at (\\d+(?:\\.\\d+)?) Gbps$", 1, workerPodIndex))
	readPerfRegx := regexp.MustCompile(fmt.Sprintf("(?m)^\\[%d,%d\\]<stdout>:.*?DACS_Cache_Client_Test:Read (\\d+) bytes .* in (\\d+(?:\\.\\d+)?) seconds at (\\d+(?:\\.\\d+)?) Gbps$", 1, workerPodIndex))
	return perf.ParseDACSClientPerfSummary(logs, writePerfRegx, readPerfRegx)
}

func (config *CacheClientConfig) getLauncherCommandArgs() []string {
	cmd := mpi.GetLauncherCommandForWorkerReadiness(config.count) + fmt.Sprintf(`exec mpirun \
			-x AZURE_CLIENT_ID \
			-x AZURE_TENANT_ID \
			-x AZURE_FEDERATED_TOKEN_FILE \
			-x AZURE_AUTHORITY_HOST \
			-x CACHE_DISCOVERY_URL \
			-x CACHE_SERVER_PORT \
			-x SI_cacheKeyExpirySeconds \
			-x POD_NAME \
			-x AZURE_STORAGE_ACCOUNT_NAME \
			--tag-output \
			--merge-stderr-to-stdout \
			-n %d \
			/bin/bash -c 'cd /app && python3 -m py_dacs_client.dacs_client_test --azBlobDirPath %s --nBlobs %d --blobSizeToBeRead %d --writeMode %s'`,
		config.count,
		fmt.Sprintf("az://%s.blob.core.windows.net/%s/${POD_NAME}-rank-${OMPI_COMM_WORLD_RANK}", config.storageAccountName, config.containerName),
		config.blobCount,
		config.blobSize,
		config.writeMode,
	)
	return []string{cmd}
}
