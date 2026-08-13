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
