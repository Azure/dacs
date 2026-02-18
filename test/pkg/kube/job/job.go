package job

import (
	"context"
	"fmt"
	"os/exec"
	"strings"
	"time"

	. "github.com/onsi/gomega"

	"github.com/Azure/dacs/test/pkg/kube/node"
	"github.com/Azure/dacs/test/pkg/kube/pod"
	"github.com/Azure/dacs/test/pkg/logger"
	"github.com/Azure/dacs/test/pkg/utils"
)

var log = logger.New()

// Delete deletes a job if it exists.
func Delete(ctx context.Context, name, ns string) error {
	cmd := exec.CommandContext(ctx, "kubectl", "delete", "job", name,
		"-n", ns, "--ignore-not-found=true")
	_, err := utils.Run(cmd)
	return err
}

// WaitForCompletion waits for a job to complete or fail.
func WaitForCompletion(ctx context.Context, name, ns string, timeout time.Duration) {
	Eventually(func(g Gomega) {
		// Check if job failed first — use StopTrying to fail immediately
		// instead of retrying for the entire timeout duration.
		failedCmd := exec.CommandContext(ctx, "kubectl", "get", "job", name,
			"-n", ns,
			"-o", "jsonpath={.status.conditions[?(@.type==\"Failed\")].status}")
		failedOutput, _ := utils.RunOutput(failedCmd)
		if failedOutput == "True" {
			dumpJobDebugInfo(ctx, name, ns)
			StopTrying("Job failed").Wrap(fmt.Errorf("job %s in namespace %s has Failed status", name, ns)).Now()
		}

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

// dumpJobDebugInfo logs diagnostic information when a job fails.
func dumpJobDebugInfo(ctx context.Context, name, ns string) {
	log.Info("========== JOB FAILURE DEBUG INFO ==========", "job", name, "namespace", ns)

	// Get job status
	jobCmd := exec.CommandContext(ctx, "kubectl", "get", "job", name, "-n", ns, "-o", "yaml")
	if output, err := utils.Run(jobCmd); err == nil {
		log.Info("Job YAML", "output", output)
	}

	// Get pod status for the job
	podListCmd := exec.CommandContext(ctx, "kubectl", "get", "pods", "-n", ns,
		"-l", fmt.Sprintf("app=%s", name), "-o", "wide")
	if output, err := utils.Run(podListCmd); err == nil {
		log.Info("Pod list", "output", output)
	}

	// Describe pods for events
	descCmd := exec.CommandContext(ctx, "kubectl", "describe", "pods", "-n", ns,
		"-l", fmt.Sprintf("app=%s", name))
	if output, err := utils.Run(descCmd); err == nil {
		log.Info("Pod describe", "output", output)
	}

	// Get pod logs
	logCmd := exec.CommandContext(ctx, "kubectl", "logs", "-n", ns,
		"-l", fmt.Sprintf("app=%s", name), "--tail=100")
	if output, err := utils.Run(logCmd); err == nil {
		log.Info("Pod logs", "output", output)
	}

	// Get cache server pod status
	csCmd := exec.CommandContext(ctx, "kubectl", "get", "pods", "-n", ns,
		"-l", "app=cacheserver", "-o", "wide")
	if output, err := utils.Run(csCmd); err == nil {
		log.Info("Cache server pods", "output", output)
	}

	log.Info("========== END DEBUG INFO ==========")
}

// GetLogs returns the logs of a job's pod.
func GetLogs(ctx context.Context, name, ns string) (string, error) {
	cmd := exec.CommandContext(ctx, "kubectl", "logs",
		fmt.Sprintf("job/%s", name),
		"-n", ns)
	return utils.Run(cmd)
}

// GetPodInfo returns the pod name, node name, and node SKU for a job's pod.
func GetPodInfo(ctx context.Context, jobName, ns string) (podName, nodeName, nodeSKU string) {
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

	nodeName = pod.GetNodeName(ctx, podName, ns)
	if nodeName == "" {
		return podName, "<unknown>", ""
	}

	nodeSKU = node.GetSKU(ctx, nodeName)
	return podName, nodeName, nodeSKU
}
