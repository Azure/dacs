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
		// Check if job failed first
		failedCmd := exec.CommandContext(ctx, "kubectl", "get", "job", name,
			"-n", ns,
			"-o", "jsonpath={.status.conditions[?(@.type==\"Failed\")].status}")
		failedOutput, _ := utils.RunOutput(failedCmd)
		g.Expect(failedOutput).NotTo(Equal("True"), "Job failed")

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
