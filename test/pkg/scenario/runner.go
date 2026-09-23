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
	BoostedBlob:   "workloads/method_boostedblob.py",
	PythonClient:  "workloads/method_python_client.py",
	Interceptor:   "workloads/method_interceptor.py",
	HFDownloader:  "workloads/method_hf_downloader.py",
	RunAIStreamer: "workloads/method_runai_streamer.py",
	Blobfuse:      "workloads/method_blobfuse.py",
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

	// Snapshot cache server metrics before the test to measure cache hit/miss delta
	metricsBefore := ScrapeCacheServerMetrics(ctx, r.Config.Namespace)

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

	// Snapshot cache server metrics after the test and compute delta
	metricsAfter := ScrapeCacheServerMetrics(ctx, r.Config.Namespace)
	delta := ComputeCacheMetricsDelta(metricsBefore, metricsAfter)
	hits, misses := delta.DownloadSuccess, delta.DownloadInvalidTransition
	if hits+misses > 0 {
		result.CacheHitRate = float64(hits) / float64(hits+misses)
		log.Info("Cache server metrics delta",
			"downloadHits", hits, "downloadMisses", misses,
			"total", hits+misses,
			"hitRate", fmt.Sprintf("%.1f%%", result.CacheHitRate*100))
	} else {
		log.Info("Cache server metrics delta", "downloadHits", 0, "downloadMisses", 0,
			"note", "no cache downloads detected (expected for no_cache mode)")
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

	// Load harness, shared auth, and method scripts
	harness, err := workloadScripts.ReadFile("workloads/harness.py")
	if err != nil {
		return nil, fmt.Errorf("failed to read harness.py: %w", err)
	}

	authScript, err := workloadScripts.ReadFile("workloads/auth.py")
	if err != nil {
		return nil, fmt.Errorf("failed to read auth.py: %w", err)
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
	if r.Method == RunAIStreamer {
		// The standard pipeline image carries the generated lock but installs
		// these large, scenario-specific packages only when they are needed.
		preCommand = `python3 -m pip install --no-cache-dir --require-hashes \
    -r /app/runai-model-streamer-requirements.txt
`
	}
	if r.Method == Interceptor {
		// Build optional cache config lines for the interceptor config file.
		// The interceptor uses cacheEnable + cacheServerDiscoveryEnabled/Endpoint
		// (NOT cacheDiscoveryUrl, which is not a valid interceptor config key).
		cacheConfigLines := ""
		if r.Cache != NoCache {
			cacheConfigLines = fmt.Sprintf(`    f.write('cacheEnable true\n')
    f.write('cacheEnableRemote true\n')
    f.write('cacheServerPort 9065\n')
    f.write('cacheServerDiscoveryEnabled true\n')
    f.write('cacheServerDiscoveryEndpoint %s\n')
    f.write('cacheServerDiscoveryIntervalInSeconds 5\n')
    f.write('cacheKeyExpirySeconds 1200\n')
    f.write('cacheIBEnabled false\n')
`, r.Config.CacheDiscoveryURL)
		}
		preCommand = fmt.Sprintf(`python3 -c "
import os
import stat
from azure.identity import DefaultAzureCredential
cred = DefaultAzureCredential()
token = cred.get_token('https://storage.azure.com/.default')
config_path = '/tmp/storageIntercept.config'
create_flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_NONBLOCK
try:
    fd = os.open(config_path, create_flags, 0o600)
except FileExistsError:
    fd = os.open(config_path, os.O_WRONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
try:
    if not stat.S_ISREG(os.fstat(fd).st_mode):
        raise ValueError('Config path is not a regular file: ' + config_path)
    os.ftruncate(fd, 0)
except Exception:
    os.close(fd)
    raise
with os.fdopen(fd, 'w') as f:
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

	scriptArg := fmt.Sprintf(`%scat > /tmp/auth.py << 'AUTH_SCRIPT_EOF'
%s
AUTH_SCRIPT_EOF
cat > /tmp/method.py << 'METHOD_SCRIPT_EOF'
%s
METHOD_SCRIPT_EOF
cat > /tmp/harness.py << 'HARNESS_SCRIPT_EOF'
%s
HARNESS_SCRIPT_EOF
python3 /tmp/harness.py`, preCommand, string(authScript), string(methodScript), string(harness))

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
			"app":                     jobName,
			"dacs.azure.com/test-pod": "true",
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
			jc.WithEnv(0, "SI_cacheEnable", "true")
			jc.WithEnv(0, "SI_cacheEnableRemote", "true")
			jc.WithEnv(0, "SI_cacheServerPort", "9065")
			jc.WithEnv(0, "SI_cacheServerDiscoveryEnabled", "true")
			jc.WithEnv(0, "SI_cacheServerDiscoveryEndpoint", cacheDiscovery)
			jc.WithEnv(0, "SI_cacheServerDiscoveryIntervalInSeconds", "5")
			jc.WithEnv(0, "SI_cacheKeyExpirySeconds", "1200")
			jc.WithEnv(0, "SI_cacheIBEnabled", "false")
			// Interceptor default streamingChunkSize is 4MB
			jc.WithEnv(0, "CACHE_CHUNK_SIZE", "4194304")
		}
	}

	// DACSCacheClient-based methods need CACHE_DISCOVERY_URL + CACHE_SERVER_PORT for cache integration.
	if (r.Method == PythonClient || r.Method == Blobfile || r.Method == BoostedBlob) && r.Cache != NoCache {
		jc.WithEnv(0, "CACHE_DISCOVERY_URL", r.Config.CacheDiscoveryURL)
		jc.WithEnv(0, "CACHE_SERVER_PORT", "9065")
		// DACSCacheClient default ChunkSize is 32MB
		jc.WithEnv(0, "CACHE_CHUNK_SIZE", "33554432")
		jc.WithEnv(0, "SI_cacheKeyGroupId", r.Scale.CacheGroupID())
	}

	// Run:AI Model Streamer: pass concurrency, block size, and cache provider.
	if r.Method == RunAIStreamer {
		if r.Config.RunAIStreamerConcurrency > 0 {
			jc.WithEnv(0, "RUNAI_STREAMER_CONCURRENCY", fmt.Sprintf("%d", r.Config.RunAIStreamerConcurrency))
		}
		if r.Config.RunAIStreamerBlockSize > 0 {
			jc.WithEnv(0, "RUNAI_STREAMER_BLOCK_SIZE", fmt.Sprintf("%d", r.Config.RunAIStreamerBlockSize))
		}
		// Debug logging for native lib auth troubleshooting
		jc.WithEnv(0, "RUNAI_STREAMER_LOG_LEVEL", "debug")
		jc.WithEnv(0, "RUNAI_STREAMER_LOG_TO_STDERR", "1")
		jc.WithEnv(0, "AZURE_LOG_LEVEL", "1") // 1=Verbose for C++ Azure SDK
		// Cache provider: tell RunAI streamer where to find libStorageDirect.so explicitly.
		// BlobSiWrapper's __attribute__((constructor)) auto-generates the SI config from env vars.
		if r.Cache != NoCache {
			jc.WithEnv(0, "RUNAI_STREAMER_EXPERIMENTAL_AZURE_CACHE_LIB", "/app/py_dacs_client/libStorageDirect.so")
			// BlobSiWrapper reads these env vars to generate the SI config:
			jc.WithEnv(0, "CACHE_DISCOVERY_URL", r.Config.CacheDiscoveryURL)
			jc.WithEnv(0, "CACHE_SERVER_PORT", "9065")
			jc.WithEnv(0, "STORAGE_INTCPT_MOUNT_PATH", "/mnt/dacs_cache")
		} else {
			// Explicitly disable cache auto-discovery for no_cache mode
			jc.WithEnv(0, "RUNAI_STREAMER_EXPERIMENTAL_AZURE_CACHE_ENABLED", "0")
		}
	}

	// Blobfuse method: pass mount paths and dist_cache (DACS L2) settings.
	// The Python module reads these env vars to render the blobfuse2 YAML config
	// at runtime.
	if r.Method == Blobfuse {
		jc.WithEnv(0, "BLOBFUSE_MOUNT_PATH", "/mnt/blobfuse").
			// Used by blobfuse azstorage.mode=workloadidentity (config key: appid)
			WithEnv(0, "AZURE_CLIENT_ID", r.Config.WorkloadIdentityClientID).
			// Privileged container is required for mount(2) and /dev/fuse access.
			WithPrivilegedFuse(0)
		if r.Cache != NoCache {
			jc.WithEnv(0, "BLOBFUSE_USE_DIST_CACHE", "true").
				WithEnv(0, "BLOBFUSE_DIST_CACHE_DISCOVERY_ENDPOINT",
					fmt.Sprintf("%s:9065", cacheDiscovery))
		}
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
