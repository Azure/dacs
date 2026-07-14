/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.
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
