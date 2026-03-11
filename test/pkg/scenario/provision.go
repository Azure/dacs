/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Data provisioning - ensures model data exists in blob storage, mirrored from Hugging Face
# --------------------------------------------------------------------------------------------------------------------
*/
package scenario

import (
	"context"
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
	"github.com/Azure/dacs/test/pkg/utils"
)

// ProvisionModelData ensures model data is available in blob storage.
// It checks if data exists, and if not, downloads from HF and uploads to blob.
func ProvisionModelData(ctx context.Context, k8sClient client.Client, config ModelLoadingConfig, scale ScaleProfile) error {
	log.Info("Provisioning model data", "scale", scale.Name, "hfRepo", scale.HFRepo, "blobPrefix", scale.BlobPrefix())

	jobName := fmt.Sprintf("scenario-provision-%s",
		strings.ToLower(strings.ReplaceAll(strings.ReplaceAll(scale.Name, "-", ""), "/", "")))
	if len(jobName) > 63 {
		jobName = jobName[:63]
	}

	// Load the embedded provision script
	provisionScript, err := workloadScripts.ReadFile("workloads/provision.py")
	if err != nil {
		return fmt.Errorf("failed to read provision.py: %w", err)
	}

	scriptArg := fmt.Sprintf(`cat > /tmp/provision.py << 'PROVISION_SCRIPT_EOF'
%s
PROVISION_SCRIPT_EOF
python3 /tmp/provision.py`, string(provisionScript))

	jobData, err := fixtures.ReadScenarioModelLoadingJob()
	if err != nil {
		return fmt.Errorf("failed to read fixture: %w", err)
	}

	var j batchv1.Job
	if err := yaml.Unmarshal(jobData, &j); err != nil {
		return fmt.Errorf("failed to unmarshal fixture: %w", err)
	}

	jc := job.NewJobConfig(&j).
		WithName(jobName).
		WithNamespace(config.Namespace).
		WithParallelism(1).
		WithCompletions(1).
		WithMatchLabels(map[string]string{
			"app":                        jobName,
			"dacs.azure.com/test-pod": "true",
		}).
		WithRestartPolicy(corev1.RestartPolicyNever).
		WithImage(config.TestImage).
		WithCommandArgs(0, []string{scriptArg}).
		WithEnv(0, "AZURE_STORAGE_ACCOUNT_NAME", config.StorageAccount).
		WithEnv(0, "STORAGE_CONTAINER", config.StorageContainer).
		WithEnv(0, "BLOB_PREFIX", scale.BlobPrefix()).
		WithEnv(0, "HF_REPO", scale.HFRepo).
		WithEnv(0, "AZURE_USE_IDENTITY", "1").
		WithEnvDownwardAPIPodName()

	// Configure credentials
	if config.WorkloadIdentityClientID != "" && config.WorkloadIdentitySA != "" {
		saYAML, err := common.WorkloadIdentitySAYAML(
			config.WorkloadIdentitySA,
			config.Namespace,
			config.WorkloadIdentityClientID)
		if err != nil {
			return fmt.Errorf("failed to create SA: %w", err)
		}
		cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
		cmd.Stdin = utils.NewStringReader(string(saYAML))
		if output, err := utils.Run(cmd); err != nil {
			return fmt.Errorf("failed to apply SA: %s: %w", output, err)
		}
		jc.WithServiceAccountName(config.WorkloadIdentitySA).
			WithWorkloadIdentityLabel()
	}

	// Clean up previous provisioning job
	if err := job.Delete(ctx, jobName, config.Namespace); err != nil {
		log.Info("Warning: failed to delete previous provision job", "error", err)
	}
	time.Sleep(2 * time.Second)

	// Create and run job
	if err := k8sClient.Create(ctx, jc.Job); err != nil {
		return fmt.Errorf("failed to create provision job: %w", err)
	}

	log.Info("Waiting for provisioning job", "job", jobName, "timeout", "60m")
	job.WaitForCompletion(ctx, jobName, config.Namespace, 60*time.Minute)

	// Check job succeeded
	var podList corev1.PodList
	if err := k8sClient.List(ctx, &podList,
		client.InNamespace(config.Namespace),
		client.MatchingLabels(jc.Job.Spec.Template.Labels)); err != nil {
		return fmt.Errorf("failed to list provision pods: %w", err)
	}

	if len(podList.Items) == 0 {
		return fmt.Errorf("no pods found for provision job %s", jobName)
	}

	p := podList.Items[0]
	logs, err := pod.GetLogs(ctx, p.Name, p.Namespace)
	if err != nil {
		log.Info("Provision job logs unavailable", "error", err)
	} else {
		log.Info("Provision job output", "logs", logs)
	}

	if !pod.IsPodExitedSuccessfully(ctx, &p) {
		return fmt.Errorf("provision job failed. Logs:\n%s", logs)
	}

	log.Info("Model data provisioned successfully", "scale", scale.Name)
	return nil
}
