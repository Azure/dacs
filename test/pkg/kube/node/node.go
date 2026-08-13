/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# Kubernetes Node helper methods
# --------------------------------------------------------------------------------------------------------------------
*/

package node

import (
	"context"
	"os/exec"
	"strings"

	"github.com/Azure/dacs/test/pkg/logger"
	"github.com/Azure/dacs/test/pkg/utils"
	"github.com/go-logr/logr"
)

var log logr.Logger

func init() {
	log = logger.New()
}

// GetSKU returns the node SKU (instance type) from node labels.
// On AKS, this comes from node.kubernetes.io/instance-type label.
func GetSKU(ctx context.Context, nodeName string) string {
	skuCmd := exec.CommandContext(ctx, "kubectl", "get", "node", nodeName,
		"-o", "jsonpath={.metadata.labels.node\\.kubernetes\\.io/instance-type}")
	skuOutput, err := utils.RunOutput(skuCmd)
	if err != nil {
		log.Error(err, "Failed to get node SKU", "node", nodeName)
		return ""
	}
	return strings.TrimSpace(skuOutput)
}

// GetWithLabel returns the names of all nodes with a specific label.
func GetWithLabel(ctx context.Context, label string) []string {
	cmd := exec.CommandContext(ctx, "kubectl", "get", "nodes",
		"-l", label,
		"-o", "jsonpath={.items[*].metadata.name}")
	output, err := utils.RunOutput(cmd)
	if err != nil {
		log.Error(err, "Failed to get nodes with label", "label", label)
		return nil
	}
	if output == "" {
		return nil
	}
	return strings.Fields(output)
}
