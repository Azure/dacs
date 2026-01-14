// Package cluster provides utilities for detecting and interacting with
// Kubernetes cluster environments.
package cluster

import (
	"os/exec"
	"strings"
)

// IsMinikube returns true if the current kubectl context is minikube.
func IsMinikube() bool {
	context, err := GetCurrentContext()
	if err != nil {
		return false
	}
	return strings.Contains(context, "minikube")
}

// GetCurrentContext returns the current kubectl context name.
func GetCurrentContext() (string, error) {
	cmd := exec.Command("kubectl", "config", "current-context")
	output, err := cmd.Output()
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(output)), nil
}
