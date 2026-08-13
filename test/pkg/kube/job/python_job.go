// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package job

import (
	"context"
	"fmt"
	"os/exec"
	"time"

	batchv1 "k8s.io/api/batch/v1"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"sigs.k8s.io/controller-runtime/pkg/client"

	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/kube/pod"
	"github.com/Azure/dacs/test/pkg/utils"
)

// PythonConfig describes a one-shot K8s Job that runs a Python script.
type PythonConfig struct {
	// Name is the K8s Job name (max 63 chars).
	Name string
	// Namespace is the K8s namespace to run in.
	Namespace string
	// Image is the container image (must have Python 3 installed).
	Image string
	// Script is the Python script body to execute.
	Script string
	// Env holds extra environment variables for the job container.
	Env map[string]string
	// Labels holds extra labels to apply to the job and pod template.
	Labels map[string]string
	// Timeout is how long to wait for the job to complete.
	Timeout time.Duration

	// StorageAccount is the Azure storage account name.
	// Automatically set as AZURE_STORAGE_ACCOUNT_NAME env var.
	StorageAccount string
	// StorageContainer is the Azure storage container name.
	// Automatically set as STORAGE_CONTAINER env var.
	StorageContainer string
	// WorkloadIdentityClientID is the Azure AD client ID for workload identity.
	WorkloadIdentityClientID string
	// WorkloadIdentitySA is the K8s ServiceAccount name for workload identity.
	WorkloadIdentitySA string
}

// RunPython creates a K8s Job that executes a Python script, waits for it
// to complete, and returns the pod logs. It handles fixture loading, workload
// identity configuration, and job lifecycle (cleanup, create, wait, verify).
func RunPython(ctx context.Context, k8sClient client.Client, cfg PythonConfig) (string, error) {
	if cfg.Timeout == 0 {
		cfg.Timeout = 10 * time.Minute
	}

	jc, err := buildPython(ctx, cfg)
	if err != nil {
		return "", err
	}

	// Clean up any previous run
	if err := Delete(ctx, cfg.Name, cfg.Namespace); err != nil {
		log.Info("Warning: failed to delete previous job", "job", cfg.Name, "error", err)
	}
	if err := WaitForDeletion(ctx, cfg.Name, cfg.Namespace, 15*time.Second); err != nil {
		return "", fmt.Errorf("timed out waiting for previous job %s to be deleted: %w", cfg.Name, err)
	}

	if err := k8sClient.Create(ctx, jc.Job); err != nil {
		return "", fmt.Errorf("failed to create job %s: %w", cfg.Name, err)
	}

	log.Info("Waiting for job", "job", cfg.Name, "timeout", cfg.Timeout)
	WaitForCompletion(ctx, cfg.Name, cfg.Namespace, cfg.Timeout)

	// Retrieve pod logs and verify success
	var podList corev1.PodList
	if err := k8sClient.List(ctx, &podList,
		client.InNamespace(cfg.Namespace),
		client.MatchingLabels(jc.Job.Spec.Template.Labels)); err != nil {
		return "", fmt.Errorf("failed to list pods for job %s: %w", cfg.Name, err)
	}
	if len(podList.Items) == 0 {
		return "", fmt.Errorf("no pods found for job %s", cfg.Name)
	}

	p := podList.Items[0]
	logs, err := pod.GetLogs(ctx, p.Name, cfg.Namespace)
	if err != nil {
		log.Info("Job logs unavailable", "job", cfg.Name, "error", err)
	}

	if !pod.IsPodExitedSuccessfully(ctx, &p) {
		return logs, fmt.Errorf("job %s failed. Logs:\n%s", cfg.Name, logs)
	}

	return logs, nil
}

// BuildPython creates a configured K8s Job without running it.
// Use this when you need to manage the job lifecycle yourself (e.g. launching
// multiple jobs concurrently before waiting for them).
func BuildPython(ctx context.Context, cfg PythonConfig) (*batchv1.Job, error) {
	jc, err := buildPython(ctx, cfg)
	if err != nil {
		return nil, err
	}
	return jc.Job, nil
}

// buildPython creates the job config without running it.
func buildPython(ctx context.Context, cfg PythonConfig) (*JobConfig, error) {
	var backoffLimit int32 = 0
	j := &batchv1.Job{
		ObjectMeta: metav1.ObjectMeta{
			Name: cfg.Name,
		},
		Spec: batchv1.JobSpec{
			BackoffLimit: &backoffLimit,
			Template: corev1.PodTemplateSpec{
				Spec: corev1.PodSpec{
					RestartPolicy: corev1.RestartPolicyNever,
					Containers: []corev1.Container{{
						Name:            "workload",
						Image:           cfg.Image,
						ImagePullPolicy: corev1.PullAlways,
						Command:         []string{"/bin/bash", "-ce"},
					}},
				},
			},
		},
	}

	scriptArg := fmt.Sprintf(`cat > /tmp/workload.py << 'WORKLOAD_SCRIPT_EOF'
%s
WORKLOAD_SCRIPT_EOF
python3 /tmp/workload.py`, cfg.Script)

	labels := map[string]string{
		"app":                        cfg.Name,
		"dacs.azure.com/test-pod": "true",
	}
	for k, v := range cfg.Labels {
		labels[k] = v
	}

	jc := NewJobConfig(j).
		WithNamespace(cfg.Namespace).
		WithParallelism(1).
		WithCompletions(1).
		WithMatchLabels(labels).
		WithCommandArgs(0, []string{scriptArg}).
		WithEnv(0, "AZURE_STORAGE_ACCOUNT_NAME", cfg.StorageAccount).
		WithEnv(0, "STORAGE_CONTAINER", cfg.StorageContainer).
		WithEnv(0, "AZURE_USE_IDENTITY", "1").
		WithEnvDownwardAPIPodName()

	for k, v := range cfg.Env {
		jc.WithEnv(0, k, v)
	}

	// Configure workload identity
	if cfg.WorkloadIdentityClientID != "" && cfg.WorkloadIdentitySA != "" {
		if err := configureWorkloadIdentity(ctx, jc, cfg.Namespace,
			cfg.WorkloadIdentityClientID, cfg.WorkloadIdentitySA); err != nil {
			return nil, err
		}
	}

	return jc, nil
}

// configureWorkloadIdentity ensures the workload identity ServiceAccount exists
// and configures the job to use it.
func configureWorkloadIdentity(ctx context.Context, jc *JobConfig, namespace, clientID, saName string) error {
	saYAML, err := common.WorkloadIdentitySAYAML(saName, namespace, clientID)
	if err != nil {
		return fmt.Errorf("failed to build workload identity SA: %w", err)
	}
	cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
	cmd.Stdin = utils.NewStringReader(string(saYAML))
	if output, err := utils.Run(cmd); err != nil {
		return fmt.Errorf("failed to apply workload identity SA: %s: %w", output, err)
	}
	jc.WithServiceAccountName(saName).WithWorkloadIdentityLabel()
	return nil
}
