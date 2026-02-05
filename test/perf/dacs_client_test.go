/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# End to End test for dacs cache clients performing writes
# --------------------------------------------------------------------------------------------------------------------
*/
package perf

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"time"

	"github.com/Azure/dacs/test/pkg/cluster"
	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/common/fixtures"
	"github.com/Azure/dacs/test/pkg/git"
	"github.com/Azure/dacs/test/pkg/helm"
	jobhelper "github.com/Azure/dacs/test/pkg/kube/job"
	"github.com/Azure/dacs/test/pkg/kube/node"
	podhelper "github.com/Azure/dacs/test/pkg/kube/pod"
	"github.com/Azure/dacs/test/pkg/kube/secret"
	"github.com/Azure/dacs/test/pkg/kusto"
	perf "github.com/Azure/dacs/test/pkg/perfsummary"
	"github.com/Azure/dacs/test/pkg/utils"
	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"go.yaml.in/yaml/v3"
	appsv1 "k8s.io/api/apps/v1"
	batchv1 "k8s.io/api/batch/v1"
	corev1 "k8s.io/api/core/v1"
	"sigs.k8s.io/controller-runtime/pkg/client"
)

var (
	cacheClientNs       = "dacs-clients"
	containerNamePrefix = "dacs-perf-"
	defaultWaitTime     = 2 * time.Minute
)

// CacheClient holds information required to deploy cacheclient
type CacheClientConfig struct {
	k8sClient client.Client // Kubernetes client to interact with cluster

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

	err := utils.CreateContainerIfNotExists(config.storageAccountName, config.containerName)
	Expect(err).ShouldNot(HaveOccurred(), "Failed to check/create container in storage account")

	config.dacsClientTest(ctx, testName)

	// Cleanup: Delete the created container
	err = utils.DeleteContainer(config.storageAccountName, config.containerName)
	Expect(err).ShouldNot(HaveOccurred(), "Failed to delete container in storage account")
}

func (config *CacheClientConfig) dacsClientTest(ctx context.Context, testName string) {
	// Read the job fixture file and modify it to create multiple dacs clients
	jobData, err := fixtures.ReadDACSCacheClientJob()
	Expect(err).NotTo(HaveOccurred(), "Failed to read cacheclient job fixture file")

	var job batchv1.Job
	err = yaml.Unmarshal(jobData, &job)
	Expect(err).ShouldNot(HaveOccurred(), "Failed to unmarshal cacheclient job fixture")

	cacheServerList, err := config.getCacheServerList()
	Expect(err).ShouldNot(HaveOccurred(), "Failed to get cacheserver list for cacheclient job")

	accessEnvKey, accessEnvValue, err := config.getStorageAccessEnv()
	Expect(err).ShouldNot(HaveOccurred(), "Failed to get storage access key/value for cacheclient job")

	// Update the job with following details
	// 1. Update the namespace
	// 2. Set completions and parallelism to config.CountLabels
	// 3. Update the PodAntiAffinity to spread across nodes
	jobConfig := jobhelper.NewJobConfig(&job).
		WithName(fmt.Sprintf("dacs-clients-%d", config.count)).
		WithNamespace(cacheClientNs).
		WithParallelism(int32(config.count)).
		WithCompletions(int32(config.count)).
		WithMatchLabels(map[string]string{"app": "write-cache-client"}).
		WithRestartPolicy(corev1.RestartPolicyNever).
		WithPodAntiAffinity().
		WithImage(config.cacheClientImage).
		WithEnv(0, "AZURE_STORAGE_ACCOUNT_NAME", config.storageAccountName).
		WithEnv(0, "CACHE_SERVER_LIST", cacheServerList).
		WithEnv(0, "CACHE_SERVER_PORT", "9065").
		WithCommandArgs(0, []string{config.getDACSCacheClientCommandArgs()}).
		WithEnvDownwardAPIPodName()

	// Configure Credentials based on access mode
	if accessEnvKey == common.CACHECLIENT_SAS_TOKEN_KEY {
		// Create Secret with SAS token in the job namespace
		secret := secret.NewSecret().
			WithName("cacheclient-secret").
			WithNamespace(job.Namespace).
			WithData(map[string]string{
				common.CACHECLIENT_SAS_TOKEN_KEY: accessEnvValue,
			})
		err = config.k8sClient.Create(ctx, secret.Secret)
		Expect(err).ShouldNot(HaveOccurred(), "Failed to create secret for cacheclient job")
		jobConfig.WithEnvFromSecret(0, secret.Secret)

	} else {
		cmd, err := getRoleAssignmentCommand(config.storageAccountName)
		Expect(err).ShouldNot(HaveOccurred(), "Failed to get role assignment command")
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
		jobConfig.WithEnv(0, common.CACHECLIENT_CLIENT_ID_KEY, accessEnvValue)
	}

	job = *jobConfig.Job

	// Create the job in the cluster
	err = config.k8sClient.Create(ctx, &job)
	Expect(err).ShouldNot(HaveOccurred(), "Failed to create cacheclient job in cluster")

	// Wait for the job to complete
	By(fmt.Sprintf("Waiting for %s job to complete", job.Name))
	jobhelper.WaitForCompletion(ctx, job.Name, job.Namespace, 15*time.Minute)

	// Fetch the write and read metrics from the all the pods of completed job
	var podList corev1.PodList
	err = config.k8sClient.List(ctx, &podList, client.InNamespace(job.Namespace), client.MatchingLabels(job.Spec.Template.Labels))
	Expect(err).ShouldNot(HaveOccurred(), "Failed to list pods for cacheclient job")

	podsMetadata := make([]kusto.PodMetadata, len(podList.Items))
	var perfSummary perf.DACSClientPerfSummary
	for i, pod := range podList.Items {
		if !podhelper.IsPodExitedSuccessfully(ctx, &pod) {
			continue
		}
		By(fmt.Sprintf("Parsing performance metrics from pod %s/%s logs", pod.Namespace, pod.Name))
		perfMetrics, err := getPerformanceMetrics(ctx, pod.Name, pod.Namespace)
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
	helmInfo := helm.GetReleaseInfo(ctx, config.cacheServerNamespace, "cache-server")
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
			JobName:        job.Name,
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
}

func (config *CacheClientConfig) getCacheServerList() (string, error) {
	// Get the cacheserver list by querying no.of cacheservers in the specified namespace
	var cacheServerSts appsv1.StatefulSet
	err := config.k8sClient.Get(context.Background(), client.ObjectKey{Namespace: config.cacheServerNamespace, Name: common.DEFAULT_CACHESERVER_NAME}, &cacheServerSts)
	if err != nil {
		return "", fmt.Errorf("Failed to get cacheserver information from namespace: %s error: %v", config.cacheServerNamespace, err)
	}
	var cacheServerList string
	for i := int32(0); i < *cacheServerSts.Spec.Replicas; i++ {
		// Format: <service-name>-<pod-index>.<service-name>.<namespace>.svc.cluster.local
		cacheServerList += fmt.Sprintf("%s-%d.%s.%s.svc.cluster.local#", common.DEFAULT_CACHESERVER_NAME, i, common.DEFAULT_CACHESERVER_SERVICE_NAME, config.cacheServerNamespace)
	}
	// Remove trailing #
	return cacheServerList[:len(cacheServerList)-1], nil
}

// getDACSCacheClientCommandArgs returns the multiline string command
func (config *CacheClientConfig) getDACSCacheClientCommandArgs() string {
	azBlobDir := fmt.Sprintf("az://%s.blob.core.windows.net/%s/${POD_NAME}/", config.storageAccountName, config.containerName)
	commandLineArgs := fmt.Sprintf(`
cd /app/
python3 -m py_dacs_client.sample.dacs_client_test --azBlobDirPath %s --nBlobs %d --blobSizeToBeRead %d --writeMode %s
`, azBlobDir, config.blobCount, config.blobSize, config.writeMode)
	return commandLineArgs
}

func getPerformanceMetrics(ctx context.Context, podName, namespace string) (*perf.DACSClientPerfSummary, error) {
	// Get the logs from the pod
	cmd := exec.CommandContext(ctx, "kubectl", "logs", podName, "-n", namespace)
	logs, err := utils.Run(cmd)
	if err != nil {
		return nil, fmt.Errorf("Failed to get logs from pod %s/%s: %v", namespace, podName, err)
	}
	return perf.ParseDACSClientPerfSummary(logs)
}

func getNodeList(client client.Client) (*corev1.NodeList, error) {
	var nodeList corev1.NodeList
	err := client.List(context.Background(), &nodeList)
	if err != nil {
		return nil, err
	}
	return &nodeList, nil
}

func (config *CacheClientConfig) getClientID() (string, error) {
	nodeList, err := getNodeList(config.k8sClient)
	if err != nil {
		return "", fmt.Errorf("Failed to get node list from cluster: %v", err)
	}
	if len(nodeList.Items) == 0 {
		return "", fmt.Errorf("No nodes found in the cluster")
	}

	for _, node := range nodeList.Items {
		if val, exists := node.Labels[common.KUBERNETES_NODE_CLIENT_ID_KEY]; exists {
			return val, nil
		}
	}
	return "", fmt.Errorf("No managed identity clientId found on any node")

}

func (config *CacheClientConfig) getStorageAccessEnv() (string, string, error) {
	value, isSASKeyExist := os.LookupEnv(common.CACHECLIENT_SAS_TOKEN_KEY)
	if isSASKeyExist {
		return common.CACHECLIENT_SAS_TOKEN_KEY, value, nil
	}
	clientID, err := config.getClientID()
	if err != nil {
		return "", "", fmt.Errorf("No storage access environment variable found for account: %s error: %v", config.storageAccountName, err)
	}
	return common.CACHECLIENT_CLIENT_ID_KEY, clientID, nil
}

func getRoleAssignmentCommand(saName string) (string, error) {
	clusterName, err := cluster.GetCurrentContext()
	if err != nil {
		return "", err
	}
	rg, err := utils.GetResourceGroupOfCluster(clusterName)
	if err != nil {
		return "", err
	}
	return "az deployment group create --resource-group " + rg + " " +
		"--template-file deploy/template/storage.bicep " +
		"--parameters storageAccountName=" + saName + " " +
		"aksName=" + clusterName + " aksResourceGroup=" + rg, nil
}
