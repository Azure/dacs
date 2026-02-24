package mpi

import (
	"context"
	"fmt"
	"os/exec"
	"time"

	"github.com/Azure/dacs/test/pkg/utils"
	v2beta1 "github.com/kubeflow/mpi-operator/pkg/apis/kubeflow/v2beta1"
	. "github.com/onsi/gomega"
	"sigs.k8s.io/yaml"
)

// MPIJob is a fluent builder for Kubeflow MPIJob resources.
type MPIJob struct {
	*v2beta1.MPIJob
}

// LoadMPIJob loads the mpi-job.yaml fixture as an MPIJob builder for easy modifications
func LoadMPIJob(readJob func() ([]byte, error)) (*MPIJob, error) {
	data, err := readJob()
	if err != nil {
		return nil, err
	}
	job := v2beta1.MPIJob{}
	if err := yaml.Unmarshal(data, &job); err != nil {
		return nil, err
	}
	return &MPIJob{MPIJob: &job}, nil
}

func (b *MPIJob) YAML() ([]byte, error) {
	// Marshal using sigs.k8s.io/yaml to honor JSON tags (apiVersion, kind, etc.).
	raw, err := yaml.Marshal(b.MPIJob)
	if err != nil {
		return nil, err
	}
	return raw, nil
}

// WaitForCompletion waits for a job to complete or fail.
func WaitForCompletion(ctx context.Context, name, ns string, timeout time.Duration) {
	Eventually(func(g Gomega) {
		// Check if job failed first
		failedCmd := exec.CommandContext(ctx, "kubectl", "get", "mpijob", name,
			"-n", ns,
			"-o", "jsonpath={.status.conditions[?(@.type==\"Failed\")].status}")
		failedOutput, err := utils.RunOutput(failedCmd)
		g.Expect(err).NotTo(HaveOccurred(), "Failed to get mpijob status")

		if failedOutput == "True" {
			return
		}
		// Check if job completed
		cmd := exec.CommandContext(ctx, "kubectl", "get", "mpijob", name,
			"-n", ns,
			"-o", "jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}")
		output, err := utils.RunOutput(cmd)
		g.Expect(err).NotTo(HaveOccurred(), "Failed to get mpijob status")
		if output == "True" {
			return
		}
		g.Expect(fmt.Errorf("Job is not yet completed or failed")).NotTo(HaveOccurred(), "Waiting for job completion")
	}).WithTimeout(timeout).WithPolling(5*time.Second).Should(Succeed(),
		fmt.Sprintf("MPIJob %s did not complete within %v", name, timeout))
}

func IsJobSucceeded(ctx context.Context, name, ns string) (bool, error) {
	cmd := exec.CommandContext(ctx, "kubectl", "get", "mpijob", name,
		"-n", ns,
		"-o", "jsonpath={.status.conditions[?(@.type==\"Succeeded\")].status}")
	output, err := utils.RunOutput(cmd)
	if err != nil {
		return false, err
	}
	return output == "True", nil
}

// Returns the command that waits till MPI workers dns resolution is ready before starting the launcher
func GetLauncherCommandForWorkerReadiness(workerCount int) string {
	return fmt.Sprintf(`
set -xuo pipefail

echo "Waiting for DNS to resolve CACHE_SERVER_LIST..."
IFS='#' read -ra HOSTS <<< "${CACHE_SERVER_LIST}"
resolve_once() {
  local h="$1"
  # Try multiple resolvers for robustness
  ssh "$h" "echo 'Connected to $h'" >/dev/null
}
wait_all() {
  local ok=0
  local service_prefix=${JOB_NAME}-worker
  local service_suffix=${JOB_NAME}.${POD_NAMESPACE}.svc
  for worker_index in $(seq 0 %d); do
    local name=$service_prefix-${worker_index}.$service_suffix
    if ! resolve_once "$name"; then
      echo "DNS not ready for $name"
      ok=1
    else
      echo "Resolved $name"
    fi
  done
  return $ok
}
attempts=60
sleep_between=5
all_resolved=false
for i in $(seq 1 "$attempts"); do
  if wait_all; then
    echo "All cache servers resolved."
    all_resolved=true
    break
  fi
  echo "Attempt $i/$attempts: retrying in ${sleep_between}s..."
  sleep "$sleep_between"
done
if ! $all_resolved; then
  echo "ERROR: DNS resolution failed after ${attempts} attempts"
  exit 1
fi

sleep 5

`, workerCount-1)
}

func (b *MPIJob) GetLauncherPodLabels() map[string]string {
	return map[string]string{
		"training.kubeflow.org/job-name":      b.ObjectMeta.Name,
		"training.kubeflow.org/job-role":      "launcher",
		"training.kubeflow.org/operator-name": "mpi-operator",
	}
}

func (b *MPIJob) GetWorkerPodLabels() map[string]string {
	return map[string]string{
		"training.kubeflow.org/job-name":      b.ObjectMeta.Name,
		"training.kubeflow.org/job-role":      "worker",
		"training.kubeflow.org/operator-name": "mpi-operator",
	}
}
