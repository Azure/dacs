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

package testutil

import (
	"fmt"
	"os/exec"
	"strings"

	. "github.com/onsi/ginkgo/v2"
)

// PrintCacheStatus prints detailed status of Cache resources for debugging.
func PrintCacheStatus(ctx SpecContext, cacheName, namespace string) {
	separator := strings.Repeat("=", 80)
	GinkgoWriter.Println("\n" + separator)
	GinkgoWriter.Println("CACHE RESOURCE STATUS")
	GinkgoWriter.Println(separator + "\n")

	// Cache CR status
	GinkgoWriter.Println("--- Cache CR ---")
	cmd := exec.CommandContext(ctx, "kubectl", "-n", namespace, "get", "cache", "-o", "wide")
	if output, err := cmd.CombinedOutput(); err == nil {
		GinkgoWriter.Println(string(output))
	} else {
		GinkgoWriter.Printf("Failed to get cache resources: %v\n", err)
	}

	cmd = exec.CommandContext(ctx, "kubectl", "-n", namespace, "get", "cache", cacheName, "-o", "yaml")
	if output, err := cmd.CombinedOutput(); err == nil {
		GinkgoWriter.Println(string(output))
	} else {
		GinkgoWriter.Printf("Failed to get cache %s: %v\n", cacheName, err)
	}

	// StatefulSet status
	GinkgoWriter.Println("\n--- StatefulSet ---")
	cmd = exec.CommandContext(ctx, "kubectl", "-n", namespace, "get", "statefulset", cacheName, "-o", "wide")
	if output, err := cmd.CombinedOutput(); err == nil {
		GinkgoWriter.Println(string(output))
	} else {
		GinkgoWriter.Printf("Failed to get statefulset: %v\n", err)
	}

	// Pods status
	GinkgoWriter.Println("\n--- Cache Server Pods ---")
	cmd = exec.CommandContext(ctx, "kubectl", "-n", namespace, "get", "pods",
		"-l", fmt.Sprintf("app=cacheserver,dacs.azure.com/cache-name=%s", cacheName), "-o", "wide")
	if output, err := cmd.CombinedOutput(); err == nil {
		GinkgoWriter.Println(string(output))
	} else {
		GinkgoWriter.Printf("Failed to get pods: %v\n", err)
	}

	// Services
	GinkgoWriter.Println("\n--- Services ---")
	cmd = exec.CommandContext(ctx, "kubectl", "-n", namespace, "get", "svc")
	if output, err := cmd.CombinedOutput(); err == nil {
		GinkgoWriter.Println(string(output))
	} else {
		GinkgoWriter.Printf("Failed to get services: %v\n", err)
	}

	// ConfigMap
	GinkgoWriter.Println("\n--- Discovery ConfigMap ---")
	cmd = exec.CommandContext(ctx, "kubectl", "-n", namespace, "get", "configmap", cacheName+"-discovery-config", "-o", "yaml")
	if output, err := cmd.CombinedOutput(); err == nil {
		GinkgoWriter.Println(string(output))
	} else {
		GinkgoWriter.Printf("Failed to get configmap: %v\n", err)
	}

	GinkgoWriter.Println(separator + "\n")
}

// PrintServerLogs prints logs from cache server pods.
func PrintServerLogs(ctx SpecContext, namespace string, tailLines int) {
	separator := strings.Repeat("=", 80)
	GinkgoWriter.Println("\n" + separator)
	GinkgoWriter.Println("CACHE SERVER LOGS")
	GinkgoWriter.Println(separator + "\n")

	// Get pod names
	cmd := exec.CommandContext(ctx, "kubectl", "-n", namespace, "get", "pods", "-l", "app=cacheserver", "-o", "jsonpath={.items[*].metadata.name}")
	output, err := cmd.CombinedOutput()
	if err != nil {
		GinkgoWriter.Printf("Failed to get pod names: %v\n", err)
		return
	}

	podNames := string(output)
	if podNames == "" {
		GinkgoWriter.Println("No cache server pods found")
		return
	}

	for _, podName := range SplitNonEmpty(podNames, " ") {
		GinkgoWriter.Printf("\n--- Logs from %s (last %d lines) ---\n", podName, tailLines)
		cmd = exec.CommandContext(ctx, "kubectl", "-n", namespace, "logs", podName, "--tail", fmt.Sprintf("%d", tailLines))
		if logOutput, err := cmd.CombinedOutput(); err == nil {
			GinkgoWriter.Println(string(logOutput))
		} else {
			GinkgoWriter.Printf("Failed to get logs: %v\n", err)
		}
	}

	GinkgoWriter.Println(separator + "\n")
}

// PrintControllerLogs prints logs from the cache controller.
func PrintControllerLogs(ctx SpecContext, controllerNamespace string, tailLines int) {
	separator := strings.Repeat("=", 80)
	GinkgoWriter.Println("\n" + separator)
	GinkgoWriter.Println("CACHE CONTROLLER LOGS")
	GinkgoWriter.Println(separator + "\n")

	cmd := exec.CommandContext(ctx, "kubectl", "-n", controllerNamespace, "logs",
		"deployment/dacs-manager", "-c", "manager", "--tail", fmt.Sprintf("%d", tailLines))
	if output, err := cmd.CombinedOutput(); err == nil {
		GinkgoWriter.Println(string(output))
	} else {
		GinkgoWriter.Printf("Failed to get controller logs: %v\n", err)
	}

	GinkgoWriter.Println(separator + "\n")
}

// PrintClientLogs prints logs from perf client jobs.
func PrintClientLogs(ctx SpecContext, namespace, jobName string) {
	separator := strings.Repeat("=", 80)
	GinkgoWriter.Println("\n" + separator)
	GinkgoWriter.Printf("CLIENT LOGS: %s\n", jobName)
	GinkgoWriter.Println(separator + "\n")

	cmd := exec.CommandContext(ctx, "kubectl", "-n", namespace, "logs", fmt.Sprintf("job/%s", jobName))
	if output, err := cmd.CombinedOutput(); err == nil {
		GinkgoWriter.Println(string(output))
	} else {
		GinkgoWriter.Printf("Failed to get client logs: %v\n", err)
	}

	GinkgoWriter.Println(separator + "\n")
}
