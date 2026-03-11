/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Model loading scenario runner - creates and manages K8s jobs for scenario tests
# --------------------------------------------------------------------------------------------------------------------
*/
package scenario

import (
	"context"
	"embed"
	"encoding/json"
	"fmt"
	"os/exec"
	"strings"
	"time"

	batchv1 "k8s.io/api/batch/v1"
	corev1 "k8s.io/api/core/v1"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/yaml"

	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/common/fixtures"
	"github.com/Azure/dacs/test/pkg/kube/job"
	"github.com/Azure/dacs/test/pkg/kube/pod"
	"github.com/Azure/dacs/test/pkg/logger"
	"github.com/Azure/dacs/test/pkg/utils"
)

//go:embed workloads/*.py
var workloadScripts embed.FS

var log = logger.New()

// methodScriptFile maps access methods to their Python module filenames.
var methodScriptFile = map[AccessMethod]string{
	Blobfile:      "workloads/method_blobfile.py",
	PythonAdapter: "workloads/method_python_adapter.py",
	Interceptor:   "workloads/method_interceptor.py",
	HFDownloader:  "workloads/method_hf_downloader.py",
}

// ModelLoadingRunner manages execution of model loading scenario jobs.
type ModelLoadingRunner struct {
	Config ModelLoadingConfig

	K8sClient client.Client

	// Test parameters
	Scale  ScaleProfile
	Method AccessMethod
	Cache  CacheMode
}

// Run executes the model loading scenario and returns the result.
func (r *ModelLoadingRunner) Run(ctx context.Context) (*ModelLoadingResult, error) {
	jobName := strings.ToLower(fmt.Sprintf("scenario-model-%s-%s-%s",
		strings.ReplaceAll(r.Scale.Name, "-", ""),
		r.Method,
		r.Cache))
	jobName = strings.ReplaceAll(jobName, "_", "-")
	// K8s name must be <= 63 chars and DNS-safe
	if len(jobName) > 63 {
		jobName = jobName[:63]
	}

	log.Info("Running model loading scenario",
		"scale", r.Scale.Name,
		"method", string(r.Method),
		"cache", string(r.Cache),
		"job", jobName)

	// Build the job
	j, err := r.buildJob(ctx, jobName)
	if err != nil {
		return nil, fmt.Errorf("failed to build job: %w", err)
	}

	// Clean up any previous run and poll until it's gone
	if err := job.Delete(ctx, jobName, r.Config.Namespace); err != nil {
		log.Info("Warning: failed to delete previous job", "error", err)
	}
	if err := job.WaitForDeletion(ctx, jobName, r.Config.Namespace, 30*time.Second); err != nil {
		log.Info("Warning: previous job cleanup timed out", "error", err)
	}

	// Create the job
	if err := r.K8sClient.Create(ctx, j); err != nil {
		return nil, fmt.Errorf("failed to create job: %w", err)
	}

	// Wait for completion
	timeout := r.estimateTimeout()
	log.Info("Waiting for job to complete", "job", jobName, "timeout", timeout)
	job.WaitForCompletion(ctx, jobName, r.Config.Namespace, timeout)

	// Get results from pod logs
	result, err := r.extractResult(ctx, j)
	if err != nil {
		return nil, err
	}

	return result, nil
}

func (r *ModelLoadingRunner) buildJob(ctx context.Context, jobName string) (*batchv1.Job, error) {
	jobData, err := fixtures.ReadScenarioModelLoadingJob()
	if err != nil {
		return nil, fmt.Errorf("failed to read fixture: %w", err)
	}

	var j batchv1.Job
	if err := yaml.Unmarshal(jobData, &j); err != nil {
		return nil, fmt.Errorf("failed to unmarshal fixture: %w", err)
	}

	// Load harness and method scripts
	harness, err := workloadScripts.ReadFile("workloads/harness.py")
	if err != nil {
		return nil, fmt.Errorf("failed to read harness.py: %w", err)
	}

	methodFile, ok := methodScriptFile[r.Method]
	if !ok {
		return nil, fmt.Errorf("no Python module for access method: %s", r.Method)
	}
	methodScript, err := workloadScripts.ReadFile(methodFile)
	if err != nil {
		return nil, fmt.Errorf("failed to read %s: %w", methodFile, err)
	}

	// Inject both scripts as heredocs; harness imports method from /tmp/method.py
	// For interceptor: get auth token and write full config BEFORE setting LD_PRELOAD.
	// The interceptor library initializes on first use, so the config must be complete
	// before any intercepted filesystem call.
	preCommand := ""
	if r.Method == Interceptor {
		// Build optional cache config lines for the interceptor config file
		cacheConfigLines := ""
		if r.Cache != NoCache {
			cacheConfigLines = fmt.Sprintf(`    f.write('cacheDiscoveryUrl %s\n')
    f.write('cacheKeyExpirySeconds 1200\n')
    f.write('cacheIBEnabled false\n')
    f.write('cacheKeyGroupId %s\n')
`, r.Config.CacheDiscoveryURL, r.Scale.CacheGroupID())
		}
		preCommand = fmt.Sprintf(`python3 -c "
from azure.identity import DefaultAzureCredential
cred = DefaultAzureCredential()
token = cred.get_token('https://storage.azure.com/.default')
with open('/tmp/storageIntercept.config', 'w') as f:
    f.write('storagePath /mnt/dacs_cache\n')
    f.write('type blob\n')
    f.write('azBlobStorageAccountName %s\n')
    f.write('azBlobStorageEndpoint https://%s.blob.core.windows.net\n')
    f.write('azBlobContainerName %s\n')
    f.write('azBlobAccessToken ' + token.token + '\n')
    f.write('maxConnsPerBlobClient 84\n')
    f.write('attributesCacheEnable true\n')
    f.write('enableWrite false\n')
%sprint('Interceptor config written')
"
export LD_PRELOAD=/app/py_dacs_client/libStorageIntercept.so
`, r.Config.StorageAccount, r.Config.StorageAccount, r.Config.StorageContainer, cacheConfigLines)
	}

	scriptArg := fmt.Sprintf(`%scat > /tmp/method.py << 'METHOD_SCRIPT_EOF'
%s
METHOD_SCRIPT_EOF
cat > /tmp/harness.py << 'HARNESS_SCRIPT_EOF'
%s
HARNESS_SCRIPT_EOF
python3 /tmp/harness.py`, preCommand, string(methodScript), string(harness))

	cacheDiscovery := ""
	if r.Cache != NoCache {
		cacheDiscovery = r.Config.CacheDiscoveryURL
	}

	cacheWarm := "false"
	if r.Cache == WarmCache {
		cacheWarm = "true"
	}

	jc := job.NewJobConfig(&j).
		WithName(jobName).
		WithNamespace(r.Config.Namespace).
		WithParallelism(1).
		WithCompletions(1).
		WithMatchLabels(map[string]string{
			"app":                           jobName,
			"dacs.azure.com/test-pod":    "true",
		}).
		WithRestartPolicy(corev1.RestartPolicyNever).
		WithImage(r.Config.TestImage).
		WithCommandArgs(0, []string{scriptArg}).
		WithEnv(0, "ACCESS_METHOD", string(r.Method)).
		WithEnv(0, "CACHE_MODE", string(r.Cache)).
		WithEnv(0, "AZURE_STORAGE_ACCOUNT_NAME", r.Config.StorageAccount).
		WithEnv(0, "STORAGE_CONTAINER", r.Config.StorageContainer).
		WithEnv(0, "BLOB_PREFIX", r.Scale.BlobPrefix()).
		WithEnv(0, "HF_REPO", r.Scale.HFRepo).
		WithEnv(0, "SCALE_NAME", r.Scale.Name).
		WithEnv(0, "CACHE_DISCOVERY_URL", cacheDiscovery).
		WithEnv(0, "CACHE_WARM", cacheWarm).
		WithEnv(0, "AZURE_USE_IDENTITY", "1").
		WithEnv(0, "READ_BUFFER_SIZE", "8589934592").
		WithEnvDownwardAPIPodName()

	// Set up storage interceptor if needed
	// Note: LD_PRELOAD is set inside the shell script (not as container env) to avoid
	// breaking bash/cat/heredoc operations. See preCommand above.
	if r.Method == Interceptor {
		jc.WithEnv(0, "STORAGE_INTERCEPT_CONFIG_FILEPATH", "/tmp/storageIntercept.config")
		if r.Cache != NoCache {
			jc.WithEnv(0, "SI_cacheDiscoveryUrl", cacheDiscovery)
			jc.WithEnv(0, "SI_cacheKeyExpirySeconds", "1200")
			jc.WithEnv(0, "SI_cacheIBEnabled", "false")
			jc.WithEnv(0, "SI_cacheKeyGroupId", r.Scale.CacheGroupID())
		}
	}

	// Python adapter needs CACHE_SERVER_LIST + CACHE_SERVER_PORT for cache integration
	if r.Method == PythonAdapter && r.Cache != NoCache {
		jc.WithEnv(0, "CACHE_SERVER_LIST", r.Config.CacheDiscoveryURL)
		jc.WithEnv(0, "CACHE_SERVER_PORT", "9065")
		jc.WithEnv(0, "SI_cacheKeyGroupId", r.Scale.CacheGroupID())
	}

	// Blobfile patcher uses the same DACSAdapter env vars for cache integration
	if r.Method == Blobfile && r.Cache != NoCache {
		jc.WithEnv(0, "CACHE_SERVER_LIST", r.Config.CacheDiscoveryURL)
		jc.WithEnv(0, "CACHE_SERVER_PORT", "9065")
		jc.WithEnv(0, "SI_cacheKeyGroupId", r.Scale.CacheGroupID())
	}

	// Configure credentials
	if r.Config.WorkloadIdentityClientID != "" && r.Config.WorkloadIdentitySA != "" {
		saYAML, err := common.WorkloadIdentitySAYAML(
			r.Config.WorkloadIdentitySA,
			r.Config.Namespace,
			r.Config.WorkloadIdentityClientID)
		if err != nil {
			return nil, fmt.Errorf("failed to create workload identity SA: %w", err)
		}
		// Apply the service account
		cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
		cmd.Stdin = utils.NewStringReader(string(saYAML))
		if output, err := utils.Run(cmd); err != nil {
			return nil, fmt.Errorf("failed to apply SA: %s: %w", output, err)
		}
		jc.WithServiceAccountName(r.Config.WorkloadIdentitySA).
			WithWorkloadIdentityLabel()
	}

	return jc.Job, nil
}

func (r *ModelLoadingRunner) extractResult(ctx context.Context, j *batchv1.Job) (*ModelLoadingResult, error) {
	var podList corev1.PodList
	if err := r.K8sClient.List(ctx, &podList,
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
		return nil, fmt.Errorf("scenario pod failed. Logs:\n%s", logs)
	}

	jsonData := extractJSONFromLogs(logs)
	if jsonData == "" {
		return nil, fmt.Errorf("failed to extract JSON from logs. Logs:\n%s", logs)
	}

	var result ModelLoadingResult
	if err := json.Unmarshal([]byte(jsonData), &result); err != nil {
		return nil, fmt.Errorf("failed to parse result JSON: %w\nJSON: %s", err, jsonData)
	}

	return &result, nil
}

func (r *ModelLoadingRunner) estimateTimeout() time.Duration {
	// Base timeout of 10 min + scale-based addition
	switch {
	case strings.HasPrefix(r.Scale.Name, "70B"):
		return 120 * time.Minute
	case strings.HasPrefix(r.Scale.Name, "13B"):
		return 45 * time.Minute
	case strings.HasPrefix(r.Scale.Name, "7B"):
		return 30 * time.Minute
	default:
		return 15 * time.Minute
	}
}

// extractJSONFromLogs extracts JSON results from pod logs between markers.
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
