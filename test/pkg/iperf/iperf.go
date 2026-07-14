// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

// Package iperf provides utilities for running iperf3 network performance tests.
package iperf

import (
	"encoding/json"
	"fmt"
	"strings"

	corev1 "k8s.io/api/core/v1"
	"sigs.k8s.io/yaml"

	"github.com/Azure/dacs/test/pkg/common/fixtures"
)

const (
	// DefaultNamespace is the default namespace for iperf pods.
	DefaultNamespace = "default"
	// DefaultPort is the default iperf3 server port.
	DefaultPort = 5201
)

// iperfJSON represents the JSON output structure from iperf3 -J
type iperfJSON struct {
	End struct {
		SumSent struct {
			Bytes         float64 `json:"bytes"`
			BitsPerSecond float64 `json:"bits_per_second"`
			Retransmits   int     `json:"retransmits"`
		} `json:"sum_sent"`
		SumReceived struct {
			Bytes         float64 `json:"bytes"`
			BitsPerSecond float64 `json:"bits_per_second"`
		} `json:"sum_received"`
	} `json:"end"`
}

// ServerConfig contains configuration for the iperf server pod.
type ServerConfig struct {
	Name      string
	Namespace string
	NodeName  string
}

// NewServerConfig creates a new ServerConfig with default values.
func NewServerConfig() *ServerConfig {
	return &ServerConfig{
		Name:      "iperf-server",
		Namespace: DefaultNamespace,
	}
}

// WithName sets the pod name.
func (c *ServerConfig) WithName(name string) *ServerConfig {
	c.Name = name
	return c
}

// WithNamespace sets the namespace.
func (c *ServerConfig) WithNamespace(ns string) *ServerConfig {
	c.Namespace = ns
	return c
}

// WithNodeName sets the node to schedule on.
func (c *ServerConfig) WithNodeName(node string) *ServerConfig {
	c.NodeName = node
	return c
}

// LoadPod loads the iperf server pod from the fixture and applies configuration.
func (c *ServerConfig) LoadPod() (*corev1.Pod, error) {
	data, err := fixtures.ReadIperfServer()
	if err != nil {
		return nil, fmt.Errorf("failed to read iperf server fixture: %w", err)
	}

	var pod corev1.Pod
	if err := yaml.Unmarshal(data, &pod); err != nil {
		return nil, fmt.Errorf("failed to unmarshal iperf server pod: %w", err)
	}

	pod.Name = c.Name
	pod.Namespace = c.Namespace
	pod.Spec.NodeName = c.NodeName

	return &pod, nil
}

// PodYAML returns the iperf server pod as YAML bytes.
func (c *ServerConfig) PodYAML() ([]byte, error) {
	pod, err := c.LoadPod()
	if err != nil {
		return nil, err
	}
	return yaml.Marshal(pod)
}

// ClientConfig contains configuration for the iperf client pod.
type ClientConfig struct {
	Name      string
	Namespace string
	NodeName  string
	ServerIP  string
	Parallel  int // Number of parallel streams (default 16)
	Duration  int // Test duration in seconds (default 10)
}

// NewClientConfig creates a new ClientConfig with default values.
func NewClientConfig() *ClientConfig {
	return &ClientConfig{
		Name:      "iperf-client",
		Namespace: DefaultNamespace,
		Parallel:  16,
		Duration:  10,
	}
}

// WithName sets the pod name.
func (c *ClientConfig) WithName(name string) *ClientConfig {
	c.Name = name
	return c
}

// WithNamespace sets the namespace.
func (c *ClientConfig) WithNamespace(ns string) *ClientConfig {
	c.Namespace = ns
	return c
}

// WithNodeName sets the node to schedule on.
func (c *ClientConfig) WithNodeName(node string) *ClientConfig {
	c.NodeName = node
	return c
}

// WithServerIP sets the server IP to connect to.
func (c *ClientConfig) WithServerIP(ip string) *ClientConfig {
	c.ServerIP = ip
	return c
}

// WithParallel sets the number of parallel streams.
func (c *ClientConfig) WithParallel(p int) *ClientConfig {
	c.Parallel = p
	return c
}

// WithDuration sets the test duration in seconds.
func (c *ClientConfig) WithDuration(d int) *ClientConfig {
	c.Duration = d
	return c
}

// LoadPod loads the iperf client pod from the fixture and applies configuration.
func (c *ClientConfig) LoadPod() (*corev1.Pod, error) {
	data, err := fixtures.ReadIperfClient()
	if err != nil {
		return nil, fmt.Errorf("failed to read iperf client fixture: %w", err)
	}

	var pod corev1.Pod
	if err := yaml.Unmarshal(data, &pod); err != nil {
		return nil, fmt.Errorf("failed to unmarshal iperf client pod: %w", err)
	}

	pod.Name = c.Name
	pod.Namespace = c.Namespace
	pod.Spec.NodeName = c.NodeName

	// Set environment variables
	if len(pod.Spec.Containers) > 0 {
		container := &pod.Spec.Containers[0]
		for i := range container.Env {
			switch container.Env[i].Name {
			case "SERVER_IP":
				container.Env[i].Value = c.ServerIP
			case "PARALLEL":
				container.Env[i].Value = fmt.Sprintf("%d", c.Parallel)
			case "DURATION":
				container.Env[i].Value = fmt.Sprintf("%d", c.Duration)
			}
		}
	}

	return &pod, nil
}

// PodYAML returns the iperf client pod as YAML bytes.
func (c *ClientConfig) PodYAML() ([]byte, error) {
	pod, err := c.LoadPod()
	if err != nil {
		return nil, err
	}
	return yaml.Marshal(pod)
}

// Result holds the parsed results from an iperf3 test.
type Result struct {
	SenderBandwidthMbps   float64
	SenderTransferMB      float64
	SenderRetransmits     int
	ReceiverBandwidthMbps float64
	ReceiverTransferMB    float64
}

// String returns a string representation of the result.
func (r *Result) String() string {
	return fmt.Sprintf("sender: bandwidth=%.2f Mbps, transfer=%.2f MB, retransmits=%d; receiver: bandwidth=%.2f Mbps, transfer=%.2f MB", r.SenderBandwidthMbps, r.SenderTransferMB, r.SenderRetransmits, r.ReceiverBandwidthMbps, r.ReceiverTransferMB)
}

// ParseOutput parses iperf3 JSON output and extracts the summary results.
// Expects output from iperf3 with -J flag (JSON format).
// The output may contain non-JSON content (e.g., package install logs) before the JSON.
func ParseOutput(output string) (*Result, error) {
	result := &Result{}

	// Find the start of JSON (first '{' character)
	// This handles cases where tdnf install output precedes the JSON
	jsonStart := strings.Index(output, "{")
	if jsonStart == -1 {
		return nil, fmt.Errorf("no JSON found in iperf3 output")
	}
	jsonOutput := output[jsonStart:]

	var data iperfJSON
	if err := json.Unmarshal([]byte(jsonOutput), &data); err != nil {
		return nil, fmt.Errorf("failed to parse iperf3 JSON output: %w", err)
	}

	// Convert bits/sec to Mbits/sec and bytes to MB
	result.SenderBandwidthMbps = data.End.SumSent.BitsPerSecond / 1_000_000
	result.SenderTransferMB = data.End.SumSent.Bytes / (1024 * 1024)
	result.SenderRetransmits = data.End.SumSent.Retransmits

	result.ReceiverBandwidthMbps = data.End.SumReceived.BitsPerSecond / 1_000_000
	result.ReceiverTransferMB = data.End.SumReceived.Bytes / (1024 * 1024)

	return result, nil
}
