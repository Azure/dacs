package perf

import (
	"context"
	"fmt"
	"strings"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"

	"github.com/Azure/dacs/test/pkg/iperf"
	"github.com/Azure/dacs/test/pkg/kube/node"
	"github.com/Azure/dacs/test/pkg/kube/pod"
	"github.com/Azure/dacs/test/pkg/kusto"
)

const (
	iperfNamespace = "default" // Use default namespace to avoid cache-server namespace requirements
)

var _ = Describe("Network Baseline", Label("perf", "iperf"), Ordered, func() {
	// Test network performance between nodes using iperf3.
	// This establishes a baseline for what network throughput looks like between nodes.

	var serverPodName string
	var serverNodeName string
	var clientNodeName string

	// Collect results for summary
	type iperfTestResult struct {
		parallel int
		result   *iperf.Result
	}
	var results []iperfTestResult

	BeforeAll(func(ctx context.Context) {
		// Deploy iperf server on the first NVMe node
		serverNodeName = getFirstNVMeNode(ctx)
		Expect(serverNodeName).NotTo(BeEmpty(), "No NVMe nodes found")

		serverPodName = "iperf-server"
		By(fmt.Sprintf("Deploying iperf3 server on node %s", serverNodeName))
		deployIperfServer(ctx, serverPodName, serverNodeName)

		By("Waiting for iperf server to be ready")
		pod.WaitForRunning(ctx, serverPodName, iperfNamespace, 3*time.Minute)
		// Give iperf3 time to install and start listening
		time.Sleep(10 * time.Second)

		// Find client node (different from server)
		nodes := getNVMeNodes(ctx)
		for _, node := range nodes {
			if node != serverNodeName {
				clientNodeName = node
				break
			}
		}
	})

	AfterAll(func(ctx context.Context) {
		// Print summary
		if len(results) > 0 {
			log.Info(strings.Repeat("=", 80))
			log.Info("IPERF TEST SUMMARY")
			log.Info(strings.Repeat("=", 80))
			log.Info(fmt.Sprintf("%-10s %-20s %-20s %15s %15s %12s",
				"Parallel", "Client Node", "Server Node", "Bandwidth(Mbps)", "Transfer(MB)", "Retransmits"))
			log.Info(strings.Repeat("-", 80))
			for _, r := range results {
				log.Info(fmt.Sprintf("%-10d %-20s %-20s %15.2f %15.2f %12d",
					r.parallel, clientNodeName, serverNodeName,
					r.result.ReceiverBandwidthMbps, r.result.ReceiverTransferMB, r.result.SenderRetransmits))
			}
			log.Info(strings.Repeat("=", 80))
		}

		By("Cleaning up iperf pods")
		_ = pod.DeleteByLabel(ctx, iperfNamespace, "dacs.azure.com/test-pod=true")
	})

	// Test cases with different parallel stream counts
	testCases := []struct {
		parallel    int
		description string
	}{
		{parallel: 1, description: "should measure network throughput with 1 stream"},
		{parallel: 16, description: "should measure network throughput with 16 parallel streams"},
	}

	for _, tc := range testCases {
		tc := tc // capture range variable
		It(tc.description, func(ctx context.Context) {
			Expect(len(getNVMeNodes(ctx))).To(BeNumerically(">=", 2), "Need at least 2 NVMe nodes for cross-node network test")
			Expect(clientNodeName).NotTo(BeEmpty(), "Could not find a different node for client")

			// Get server pod IP
			serverIP := pod.GetIP(ctx, serverPodName, iperfNamespace)
			Expect(serverIP).NotTo(BeEmpty(), "Failed to get server pod IP")

			By(fmt.Sprintf("Running iperf3 client (parallel=%d) on node %s -> server on %s", tc.parallel, clientNodeName, serverNodeName))
			clientConfig := iperf.NewClientConfig().
				WithName(fmt.Sprintf("iperf-client-p%d", tc.parallel)).
				WithNamespace(iperfNamespace).
				WithNodeName(clientNodeName).
				WithServerIP(serverIP).
				WithParallel(tc.parallel)
			result := runIperfClient(ctx, clientConfig)

			log.Info(strings.Repeat("=", 80))
			log.Info(fmt.Sprintf("CROSS-NODE NETWORK BASELINE (iperf3, parallel=%d)", tc.parallel))
			log.Info(strings.Repeat("=", 80))
			log.Info("Network Test Result (Sender)", "parallel", tc.parallel, "bandwidthMbps", fmt.Sprintf("%.2f", result.SenderBandwidthMbps), "transferMB", fmt.Sprintf("%.2f", result.SenderTransferMB), "retransmits", result.SenderRetransmits)
			log.Info("Network Test Result (Receiver)", "parallel", tc.parallel, "bandwidthMbps", fmt.Sprintf("%.2f", result.ReceiverBandwidthMbps), "transferMB", fmt.Sprintf("%.2f", result.ReceiverTransferMB))
			log.Info(strings.Repeat("=", 80))

			// Send metrics to Kusto via EventHub
			By("Sending iperf metrics to Kusto")
			sendIperfMetrics(ctx, clientNodeName, clientConfig.Name, clientConfig, result)

			// Collect result for summary
			results = append(results, iperfTestResult{parallel: tc.parallel, result: result})

			// Basic sanity check - should get at least some throughput
			Expect(result.SenderBandwidthMbps).To(BeNumerically(">", 0), "Expected non-zero sender bandwidth")
			Expect(result.ReceiverBandwidthMbps).To(BeNumerically(">", 0), "Expected non-zero receiver bandwidth")
		})
	}
})

const nvmeNodeLabel = "dacs.azure.com/processing-unit=cpu"

// getFirstNVMeNode returns the name of the first node with the NVMe label.
func getFirstNVMeNode(ctx context.Context) string {
	nodes := node.GetWithLabel(ctx, nvmeNodeLabel)
	if len(nodes) > 0 {
		return nodes[0]
	}
	return ""
}

// getNVMeNodes returns the names of all nodes with the NVMe processing unit label.
func getNVMeNodes(ctx context.Context) []string {
	return node.GetWithLabel(ctx, nvmeNodeLabel)
}

// deployIperfServer deploys an iperf3 server pod on the specified node.
func deployIperfServer(ctx context.Context, name, nodeName string) {
	config := iperf.NewServerConfig().
		WithName(name).
		WithNamespace(iperfNamespace).
		WithNodeName(nodeName)

	podYAML, err := config.PodYAML()
	Expect(err).NotTo(HaveOccurred(), "Failed to generate iperf server YAML")

	err = pod.Apply(ctx, string(podYAML))
	Expect(err).NotTo(HaveOccurred(), "Failed to deploy iperf server")
}

// runIperfClient runs an iperf3 client as a pod and returns the results.
// It retries up to maxIperfRetries times on transient failures (no JSON output or zero bandwidth).
func runIperfClient(ctx context.Context, config *iperf.ClientConfig) *iperf.Result {
	const maxIperfRetries = 3

	var lastErr error
	var result *iperf.Result

	for attempt := 1; attempt <= maxIperfRetries; attempt++ {
		if attempt > 1 {
			log.Info("Retrying iperf client", "attempt", attempt, "maxRetries", maxIperfRetries, "lastError", lastErr)
			time.Sleep(5 * time.Second)
		}

		// Delete any existing client pod
		_ = pod.Delete(ctx, config.Name, config.Namespace)

		podYAML, err := config.PodYAML()
		Expect(err).NotTo(HaveOccurred(), "Failed to generate iperf client YAML")

		err = pod.Apply(ctx, string(podYAML))
		Expect(err).NotTo(HaveOccurred(), "Failed to create iperf client")

		// Wait for client to complete (needs more time for tdnf install)
		pod.WaitForComplete(ctx, config.Name, config.Namespace, 3*time.Minute)

		// Get logs and parse results
		logsOutput, err := pod.GetLogs(ctx, config.Name, config.Namespace)
		if err != nil {
			lastErr = fmt.Errorf("failed to get iperf client logs: %w", err)
			continue
		}

		result, err = iperf.ParseOutput(logsOutput)
		if err != nil {
			lastErr = fmt.Errorf("failed to parse iperf3 JSON output: %w", err)
			continue
		}

		// Retry if bandwidth is zero (transient networking issue)
		if result.SenderBandwidthMbps == 0 && result.ReceiverBandwidthMbps == 0 {
			lastErr = fmt.Errorf("iperf3 reported zero bandwidth (sender=%.2f, receiver=%.2f Mbps)",
				result.SenderBandwidthMbps, result.ReceiverBandwidthMbps)
			continue
		}

		// Success
		if attempt > 1 {
			log.Info("iperf client succeeded after retry", "attempt", attempt)
		}
		return result
	}

	// All retries exhausted — fail with last error
	Fail(fmt.Sprintf("iperf client failed after %d attempts: %v", maxIperfRetries, lastErr))
	return nil // unreachable
}

// sendIperfMetrics sends iperf test results to Kusto via EventHub.
func sendIperfMetrics(ctx context.Context, clientNode, clientPodName string, config *iperf.ClientConfig, result *iperf.Result) {
	nodeSKU := node.GetSKU(ctx, clientNode)
	metric := newPerfMetricBase(ctx, clientPodName, clientNode, nodeSKU)
	metric.Result = kusto.IperfResult{
		DurationSeconds:       config.Duration,
		ParallelStreams:       config.Parallel,
		SenderBandwidthMbps:   result.SenderBandwidthMbps,
		SenderTransferMB:      result.SenderTransferMB,
		SenderRetransmits:     result.SenderRetransmits,
		ReceiverBandwidthMbps: result.ReceiverBandwidthMbps,
		ReceiverTransferMB:    result.ReceiverTransferMB,
	}

	err := kusto.SendPerfMetric(ctx, kusto.DefaultEventHubConfig, metric)
	if err != nil {
		log.Error(err, "Failed to send iperf metric (non-fatal)")
	}
}
