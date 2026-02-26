package pod

import (
	"context"
	"fmt"
	"os/exec"
	"strings"
	"time"

	. "github.com/onsi/gomega"
	corev1 "k8s.io/api/core/v1"

	"github.com/Azure/dacs/test/pkg/logger"
	"github.com/Azure/dacs/test/pkg/utils"
)

var log = logger.New()

// GetIP returns the IP address of a pod.
func GetIP(ctx context.Context, name, ns string) string {
	cmd := exec.CommandContext(ctx, "kubectl", "get", "pod", name,
		"-n", ns,
		"-o", "jsonpath={.status.podIP}")
	output, err := utils.RunOutput(cmd)
	if err != nil {
		log.Error(err, "Failed to get pod IP", "pod", name)
		return ""
	}
	return strings.TrimSpace(output)
}

// GetPhase returns the phase of a pod (Pending, Running, Succeeded, Failed, Unknown).
func GetPhase(ctx context.Context, name, ns string) (string, error) {
	cmd := exec.CommandContext(ctx, "kubectl", "get", "pod", name,
		"-n", ns,
		"-o", "jsonpath={.status.phase}")
	output, err := utils.RunOutput(cmd)
	if err != nil {
		return "", fmt.Errorf("failed to get pod phase: %w", err)
	}
	return strings.TrimSpace(output), nil
}

// GetNodeName returns the name of the node a pod is scheduled on.
func GetNodeName(ctx context.Context, name, ns string) string {
	cmd := exec.CommandContext(ctx, "kubectl", "get", "pod", name,
		"-n", ns,
		"-o", "jsonpath={.spec.nodeName}")
	output, err := utils.RunOutput(cmd)
	if err != nil {
		log.Error(err, "Failed to get pod node name", "pod", name)
		return ""
	}
	return strings.TrimSpace(output)
}

// GetLogs returns the logs of a pod.
func GetLogs(ctx context.Context, name, ns string) (string, error) {
	cmd := exec.CommandContext(ctx, "kubectl", "logs", name, "-n", ns)
	return utils.RunOutput(cmd)
}

// ReadFile reads a file from a pod using kubectl exec.
func ReadFile(ctx context.Context, name, ns, filePath string) (string, error) {
	cmd := exec.CommandContext(ctx, "kubectl", "exec", name,
		"-n", ns, "--", "cat", filePath)
	return utils.RunOutput(cmd)
}

// WaitForRunning waits for a pod to be in the Running phase.
func WaitForRunning(ctx context.Context, name, ns string, timeout time.Duration) {
	Eventually(func(g Gomega) {
		phase, err := GetPhase(ctx, name, ns)
		g.Expect(err).NotTo(HaveOccurred(), "Failed to get pod status")
		g.Expect(phase).To(Equal("Running"), "Pod not running")
	}).WithTimeout(timeout).WithPolling(2*time.Second).Should(Succeed(),
		fmt.Sprintf("Pod %s did not start running within %v", name, timeout))
}

// WaitForComplete waits for a pod to complete (Succeeded or Failed phase).
func WaitForComplete(ctx context.Context, name, ns string, timeout time.Duration) {
	Eventually(func(g Gomega) {
		phase, err := GetPhase(ctx, name, ns)
		g.Expect(err).NotTo(HaveOccurred(), "Failed to get pod status")
		g.Expect(phase).To(Or(Equal("Succeeded"), Equal("Failed")), "Pod still running")
	}).WithTimeout(timeout).WithPolling(2*time.Second).Should(Succeed(),
		fmt.Sprintf("Pod %s did not complete within %v", name, timeout))
}

// Delete deletes a pod if it exists.
func Delete(ctx context.Context, name, ns string) error {
	cmd := exec.CommandContext(ctx, "kubectl", "delete", "pod", name,
		"-n", ns, "--ignore-not-found=true")
	_, err := utils.Run(cmd)
	return err
}

// DeleteByLabel deletes all pods matching a label selector.
func DeleteByLabel(ctx context.Context, ns, labelSelector string) error {
	cmd := exec.CommandContext(ctx, "kubectl", "delete", "pods",
		"-n", ns,
		"-l", labelSelector,
		"--ignore-not-found=true")
	output, err := utils.Run(cmd)
	if err != nil {
		log.Error(err, "Failed to delete pods by label", "selector", labelSelector, "output", output)
	}
	return err
}

// Apply applies a YAML manifest to the cluster.
func Apply(ctx context.Context, yaml string) error {
	cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
	cmd.Stdin = utils.NewStringReader(yaml)
	output, err := utils.Run(cmd)
	if err != nil {
		log.Error(err, "Failed to apply YAML", "output", output)
	}
	return err
}

func IsPodExitedSuccessfully(ctx context.Context, pod *corev1.Pod) bool {
	var reason, message string
	var exitCode int32
	for _, cs := range pod.Status.ContainerStatuses {
		if cs.State.Terminated != nil {
			reason = cs.State.Terminated.Reason
			message = cs.State.Terminated.Message
			exitCode = cs.State.Terminated.ExitCode
			if cs.State.Terminated.ExitCode == 0 {
				return true
			}
			log.Error(fmt.Errorf("pod %s container %s exited with reason: %s, message: %s, exitCode: %d",
				pod.Name, cs.Name, reason, message, exitCode), "Container exited with non-zero exit code")
		}
	}
	return false
}

// Exec executes a command in a pod and returns the output.
func Exec(ctx context.Context, name, ns, cmd string) (string, error) {
	kubectlCmd := exec.CommandContext(ctx, "kubectl", "exec", name,
		"-n", ns, "--", "bash", "-c", cmd)
	return utils.RunOutput(kubectlCmd)
}

// CopyTo copies a local file to a pod.
func CopyTo(ctx context.Context, localPath, name, ns, remotePath string) error {
	dstPath := fmt.Sprintf("%s/%s:%s", ns, name, remotePath)
	cmd := exec.CommandContext(ctx, "kubectl", "cp", localPath, dstPath)
	_, err := utils.Run(cmd)
	return err
}

// CopyFrom copies a file from a pod to local path.
func CopyFrom(ctx context.Context, name, ns, remotePath, localPath string) error {
	srcPath := fmt.Sprintf("%s/%s:%s", ns, name, remotePath)
	cmd := exec.CommandContext(ctx, "kubectl", "cp", srcPath, localPath)
	_, err := utils.Run(cmd)
	return err
}

// MkDir creates a directory in a pod.
func MkDir(ctx context.Context, name, ns, path string) error {
	cmd := exec.CommandContext(ctx, "kubectl", "exec", name,
		"-n", ns, "--", "mkdir", "-p", path)
	_, err := utils.Run(cmd)
	return err
}
