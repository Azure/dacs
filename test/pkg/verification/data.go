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

package verification

import (
	"context"
	"fmt"
	"os/exec"
	"time"

	"github.com/go-logr/logr"
	"sigs.k8s.io/controller-runtime/pkg/client"

	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/kube/job"
	"github.com/Azure/dacs/test/pkg/perfclient"
	"github.com/Azure/dacs/test/pkg/utils"
)

// DataPrefix identifies a previously uploaded dataset for data-integrity verification.
type DataPrefix string

// defaultConnectionPoolSize is used when discovery is enabled and the number
// of cache servers is unknown. 32 covers up to 32 servers with 1 thread.
const defaultConnectionPoolSize = 32

// defaultJobTimeout is the maximum time to wait for a perf client job.
const defaultJobTimeout = 10 * time.Minute

// UploadTestData uploads test data to the cache and returns a prefix that can
// be used to download and verify the data later. This proves data survived
// a disruption when called before/after a test.
func UploadTestData(ctx context.Context, k8sClient client.Client, namespace string, log logr.Logger) (DataPrefix, error) {
	prefix := fmt.Sprintf("t3-%d", time.Now().UnixMilli())
	config := perfclient.NewConfig().
		WithMode(perfclient.ModeUpload).
		WithNamespace(namespace).
		WithCacheName(common.GetCacheName(ctx, k8sClient)).
		WithCalculateChecksum(true).
		WithPrefix(prefix).
		WithConnectionPoolSize(defaultConnectionPoolSize).
		WithAllowColocation(true)

	if err := runPerfJob(ctx, config, log); err != nil {
		return "", fmt.Errorf("data upload failed: %w", err)
	}
	log.Info("test data uploaded", "prefix", prefix, "namespace", namespace)
	return DataPrefix(prefix), nil
}

// DownloadAndVerify downloads previously uploaded data and validates checksums.
// Returns an error if any chunks are missing or corrupted.
func DownloadAndVerify(ctx context.Context, k8sClient client.Client, namespace string, prefix DataPrefix, log logr.Logger) error {
	config := perfclient.NewConfig().
		WithMode(perfclient.ModeDownload).
		WithNamespace(namespace).
		WithCacheName(common.GetCacheName(ctx, k8sClient)).
		WithValidateChecksum(true).
		WithPrefix(string(prefix)).
		WithConnectionPoolSize(defaultConnectionPoolSize).
		WithAllowColocation(true)

	if err := runPerfJob(ctx, config, log); err != nil {
		return fmt.Errorf("data download+verify failed: %w", err)
	}
	log.Info("data integrity verified", "prefix", prefix, "namespace", namespace)
	return nil
}

// VerifyCacheServing performs a fresh upload + download cycle to prove the cache
// works end-to-end. Use this when prior data may have been lost (e.g., nodes
// replaced during upgrade).
func VerifyCacheServing(ctx context.Context, k8sClient client.Client, namespace string, log logr.Logger) error {
	prefix, err := UploadTestData(ctx, k8sClient, namespace, log)
	if err != nil {
		return fmt.Errorf("fresh verification upload failed: %w", err)
	}
	if err := DownloadAndVerify(ctx, k8sClient, namespace, prefix, log); err != nil {
		return fmt.Errorf("fresh verification download failed: %w", err)
	}
	log.Info("cache serving verified end-to-end", "namespace", namespace)
	return nil
}

// runPerfJob creates and runs a perf client Kubernetes Job, waiting for completion.
func runPerfJob(ctx context.Context, config *perfclient.Config, log logr.Logger) error {
	k8sJob, err := config.LoadJob()
	if err != nil {
		return fmt.Errorf("failed to load job: %w", err)
	}

	// Delete any existing job and wait for full removal to avoid "object is
	// being deleted" conflicts on the subsequent apply.
	delCmd := exec.CommandContext(ctx, "kubectl", "delete", "job", k8sJob.Name,
		"-n", config.Namespace, "--ignore-not-found", "--wait")
	if output, delErr := utils.Run(delCmd); delErr != nil {
		return fmt.Errorf("failed to delete existing job %s: %s: %w", k8sJob.Name, output, delErr)
	}

	jobYAML, err := config.JobYAML()
	if err != nil {
		return fmt.Errorf("failed to generate job YAML: %w", err)
	}

	cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
	cmd.Stdin = utils.NewStringReader(string(jobYAML))
	output, err := utils.Run(cmd)
	if err != nil {
		return fmt.Errorf("failed to apply job: %s: %w", output, err)
	}

	log.Info("waiting for perf job", "job", k8sJob.Name, "mode", config.Mode)
	// WaitForCompletion uses Gomega assertions - job failure will fail the
	// Ginkgo test directly via StopTrying.
	job.WaitForCompletion(ctx, k8sJob.Name, config.Namespace, defaultJobTimeout)
	return nil
}
