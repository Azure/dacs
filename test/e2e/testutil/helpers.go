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
	. "github.com/onsi/gomega"
)

// BuildCacheServerHostnames constructs a #-delimited list of cache server hostnames
// for the given StatefulSet, service, namespace and number of replicas.
func BuildCacheServerHostnames(statefulSetName, serviceName, namespace string, replicas int) string {
	hostnames := make([]string, replicas)
	for i := 0; i < replicas; i++ {
		hostnames[i] = fmt.Sprintf("%s-%d.%s.%s.svc.cluster.local",
			statefulSetName, i, serviceName, namespace)
	}
	return strings.Join(hostnames, "#")
}

// GetCacheStatus retrieves the Cache CR status conditions as a JSON string.
func GetCacheStatus(ctx SpecContext, name, namespace string) string {
	cmd := exec.CommandContext(ctx, "kubectl", "-n", namespace,
		"get", "cache", name, "-o", "jsonpath={.status.conditions}")
	output, err := cmd.CombinedOutput()
	Expect(err).NotTo(HaveOccurred())
	return string(output)
}

// ScaleCache patches a Cache CR to the specified number of replicas.
func ScaleCache(ctx SpecContext, name, namespace string, replicas int) {
	cmd := exec.CommandContext(ctx, "kubectl", "-n", namespace,
		"patch", "cache", name, "--type=json",
		"-p", fmt.Sprintf(`[{"op": "replace", "path": "/spec/scaling/static/numServers", "value": %d}]`, replicas))
	output, err := cmd.CombinedOutput()
	Expect(err).NotTo(HaveOccurred(), "Failed to scale cache: %s", string(output))
}

// GetJobLogs retrieves logs from a completed Kubernetes Job.
func GetJobLogs(ctx SpecContext, jobName, namespace string) string {
	cmd := exec.CommandContext(ctx, "kubectl", "-n", namespace,
		"logs", fmt.Sprintf("job/%s", jobName))
	output, err := cmd.CombinedOutput()
	Expect(err).NotTo(HaveOccurred())
	return string(output)
}

// SplitNonEmpty splits a string and returns non-empty trimmed parts.
func SplitNonEmpty(s, sep string) []string {
	var result []string
	for _, part := range strings.Split(s, sep) {
		if trimmed := strings.TrimSpace(part); trimmed != "" {
			result = append(result, trimmed)
		}
	}
	return result
}
