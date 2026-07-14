// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

package configmap

import (
	"context"
	"fmt"
	"os/exec"
	"strings"

	. "github.com/onsi/gomega"
	corev1 "k8s.io/api/core/v1"
	"sigs.k8s.io/yaml"

	"github.com/Azure/dacs/test/pkg/logger"
	"github.com/Azure/dacs/test/pkg/utils"
)

var log = logger.New()

// LoadFromBytes unmarshals YAML bytes into a ConfigMap.
func LoadFromBytes(data []byte) (*corev1.ConfigMap, error) {
	var cm corev1.ConfigMap
	if err := yaml.Unmarshal(data, &cm); err != nil {
		return nil, fmt.Errorf("failed to unmarshal configmap: %w", err)
	}
	return &cm, nil
}

// Get retrieves a ConfigMap by name and namespace.
func Get(ctx context.Context, name, ns string) (string, error) {
	cmd := exec.CommandContext(ctx, "kubectl", "get", "configmap", name,
		"-n", ns,
		"-o", "yaml")
	return utils.RunOutput(cmd)
}

// Create creates a ConfigMap from YAML string.
func Create(ctx context.Context, yamlContent string) error {
	cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
	cmd.Stdin = strings.NewReader(yamlContent)
	output, err := utils.Run(cmd)
	if err != nil {
		log.Error(err, "Failed to create ConfigMap", "output", output)
	}
	return err
}

// Delete deletes a ConfigMap if it exists.
func Delete(ctx context.Context, name, ns string) error {
	cmd := exec.CommandContext(ctx, "kubectl", "delete", "configmap", name,
		"-n", ns, "--ignore-not-found=true")
	_, err := utils.Run(cmd)
	return err
}

// CreateOrUpdate creates a ConfigMap, deleting any existing one first.
func CreateOrUpdate(ctx context.Context, name, ns, yamlContent string) error {
	// Delete existing ConfigMap if present
	if err := Delete(ctx, name, ns); err != nil {
		return fmt.Errorf("failed to delete existing configmap: %w", err)
	}
	return Create(ctx, yamlContent)
}

// Exists checks if a ConfigMap exists.
func Exists(ctx context.Context, name, ns string) bool {
	cmd := exec.CommandContext(ctx, "kubectl", "get", "configmap", name,
		"-n", ns, "--ignore-not-found=true",
		"-o", "name")
	output, err := utils.RunOutput(cmd)
	if err != nil {
		return false
	}
	return strings.TrimSpace(output) != ""
}

// WaitForExists waits for a ConfigMap to exist.
func WaitForExists(ctx context.Context, name, ns string) {
	Eventually(func(g Gomega) {
		g.Expect(Exists(ctx, name, ns)).To(BeTrue(), "ConfigMap %s/%s does not exist", ns, name)
	}).Should(Succeed(), fmt.Sprintf("ConfigMap %s/%s did not appear", ns, name))
}
