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

package kusto

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"time"

	"github.com/Azure/dacs/test/pkg/eventhub"
	"github.com/Azure/dacs/test/pkg/logger"
)

// log is the default logger for the kusto package.
var log = logger.New()

var (
	// Pipeline variables for RunID
	buildDefinitionName = os.Getenv("BUILD_DEFINITION_NAME")
	buildNum            = os.Getenv("BUILD_NUM")

	// SkipSendMetrics controls whether metrics are sent to EventHub/Kusto.
	// By default, metrics are NOT sent. Set SEND_METRICS=true to enable sending metrics.
	SkipSendMetrics = os.Getenv("SEND_METRICS") != "true"
)

// EventHubConfig holds the configuration for sending metrics to EventHub.
type EventHubConfig struct {
	Namespace string
	Name      string
}

// DefaultEventHubConfig returns the default EventHub configuration for DACS perf metrics.
var DefaultEventHubConfig = EventHubConfig{
	Namespace: os.Getenv("EVENTHUB_NAMESPACE"),
	Name:      os.Getenv("EVENTHUB_NAME"),
}

// PerfMetric represents a performance metric to be sent to Kusto via EventHub.
type PerfMetric struct {
	// Top-level indexing fields
	RunID            string `json:"RunID"`
	SuiteID          string `json:"SuiteID"`
	Timestamp        string `json:"Timestamp"`
	TestName         string `json:"TestName"`
	TestGitCommit    string `json:"TestGitCommit"`
	TestGitBranch    string `json:"TestGitBranch"`
	HelmChartVersion string `json:"HelmChartVersion"`
	HelmAppVersion   string `json:"HelmAppVersion"`

	// TestMetadata contains dynamic test environment info
	TestMetadata any `json:"TestMetadata"`

	// Result contains the performance data (PerfResult / IperfResult / DACSClientPerfResult)
	Result any `json:"Result"`
}

// TestMetadata contains test environment metadata (dynamic column in Kusto).
type TestMetadata struct {
	ClusterName string `json:"ClusterName"`
	PodName     string `json:"PodName"`
	NodeName    string `json:"NodeName"`
	NodeSKU     string `json:"NodeSKU"`
	IsMinikube  bool   `json:"IsMinikube"`
}

// AdapterPerfResult contains the adapter benchmark results (dynamic column in Kusto).
type AdapterPerfResult struct {
	ClientName            string  `json:"ClientName"`
	BlobSizeGB            float64 `json:"BlobSizeGB"`
	NumBlobs              int     `json:"NumBlobs"`
	NumProcesses          int     `json:"NumProcesses"`
	WriteMode             string  `json:"WriteMode"`
	TotalBytesTransferred uint64  `json:"TotalBytesTransferred"`
	DurationSeconds       float64 `json:"DurationSeconds"`
	ThroughputMbps        float64 `json:"ThroughputMbps"`
}

// PerfResult contains the cache server performance test results (dynamic column in Kusto).
type PerfResult struct {
	Mode                    string `json:"Mode"`
	ClientType              string `json:"ClientType"`
	ChunkSizeBytes          int64  `json:"ChunkSizeBytes"`
	DurationMs              int64  `json:"DurationMs"`
	TotalBytesTransferred   int64  `json:"TotalBytesTransferred"`
	ThroughputMbps          int64  `json:"ThroughputMbps"`
	TotalFailures           int    `json:"TotalFailures"`
	ExpectedChunks          int    `json:"ExpectedChunks"`
	SuccessfulChunks        int    `json:"SuccessfulChunks"`
	PerChunkP50Ms           int64  `json:"PerChunkP50Ms"`
	PerChunkP90Ms           int64  `json:"PerChunkP90Ms"`
	PerChunkP99Ms           int64  `json:"PerChunkP99Ms"`
	NumThreads              int    `json:"NumThreads"`
	DataSizeGiB             int    `json:"DataSizeGiB"`
	NumIterations           int    `json:"NumIterations"`
	NumServers              int    `json:"NumServers"`
	EnableCalculateChecksum bool   `json:"EnableCalculateChecksum"`
	EnableValidateChecksum  bool   `json:"EnableValidateChecksum"`
	ChecksumAlgorithm       string `json:"ChecksumAlgorithm"`
	StorageAccount          string `json:"StorageAccount,omitempty"`
	StorageContainer        string `json:"StorageContainer,omitempty"`
}

// IperfResult contains the iperf3 network baseline test results (dynamic column in Kusto).
type IperfResult struct {
	DurationSeconds       int     `json:"DurationSeconds"`
	ParallelStreams       int     `json:"ParallelStreams"`
	SenderBandwidthMbps   float64 `json:"SenderBandwidthMbps"`
	SenderTransferMB      float64 `json:"SenderTransferMB"`
	SenderRetransmits     int     `json:"SenderRetransmits"`
	ReceiverBandwidthMbps float64 `json:"ReceiverBandwidthMbps"`
	ReceiverTransferMB    float64 `json:"ReceiverTransferMB"`
}

// DACSClientMetadata contains test environment metadata specific to DACS client tests
type DACSClientMetadata struct {
	StorageAccount string        `json:"StorageAccount"`
	ContainerName  string        `json:"ContainerName"`
	ClusterName    string        `json:"ClusterName"`
	JobName        string        `json:"JobName"`
	IsMinikube     bool          `json:"IsMinikube"`
	WriteMode      string        `json:"WriteMode"`
	Clients        int           `json:"Clients"`
	PodsMetadata   []PodMetadata `json:",inline"`
}

// PodMetadata contains pod-specific metadata for DACS client tests
type PodMetadata struct {
	PodName     string                  `json:"PodName"`
	NodeName    string                  `json:"NodeName"`
	NodeSKU     string                  `json:"NodeSKU"`
	PerfSummary DACSClientPerfResult `json:"PerfSummary"`
}

// DACSClientPerfResult contains accumulated performance results of specific DACS client tests
type DACSClientPerfResult struct {
	BlobSize          int64  `json:"BlobSize"`
	TotalBlobsWritten uint64 `json:"TotalBlobsWritten"`

	// Write related metrics
	TotalBytesWritten     uint64  `json:"TotalBytesWritten"`
	TotalWriteTimeSeconds float64 `json:"TotalWriteTimeSeconds"`
	WriteThroughputGbps   float64 `json:"WriteThroughputGbps"`

	// Read related metrics
	TotalBytesRead       uint64  `json:"TotalBytesRead"`
	TotalReadTimeSeconds float64 `json:"TotalReadTimeSeconds"`
	ReadThroughputGbps   float64 `json:"ReadThroughputGbps"`
}

// ResilienceOperationResult contains the timing for a single AKS resilience
// operation (e.g. helm upgrade, helm rollback, AKS cluster upgrade, node-pool
// scale up/down) as a dynamic column in Kusto.
type ResilienceOperationResult struct {
	// Operation identifies the operation measured, e.g. "helm-upgrade",
	// "helm-rollback", "aks-upgrade", "scale-down", "scale-up".
	Operation       string  `json:"Operation"`
	DurationSeconds float64 `json:"DurationSeconds"`
	// Details holds operation-specific context and is only emitted for
	// operations that have it (e.g. UpgradeOperationDetails for upgrades,
	// ScaleOperationDetails for scales).
	Details any `json:"Details,omitempty"`
}

// UpgradeOperationDetails holds context specific to upgrade-style operations.
type UpgradeOperationDetails struct {
	TargetVersion string `json:"TargetVersion"`
}

// ScaleOperationDetails holds context specific to scale-style operations.
type ScaleOperationDetails struct {
	TargetNodes int `json:"TargetNodes"`
}

// ResilienceSpecResult records the outcome of an AKS resilience test spec
// (dynamic column in Kusto) so dashboards can track pass/fail trends.
type ResilienceSpecResult struct {
	// Outcome is the spec outcome: "passed", "failed", or "skipped".
	Outcome         string  `json:"Outcome"`
	DurationSeconds float64 `json:"DurationSeconds"`
	// FailureMessage is the spec failure message, set only when Outcome is "failed".
	FailureMessage string `json:"FailureMessage,omitempty"`
}

// ScenarioResult contains workload scenario performance results (dynamic column in Kusto).
type ScenarioResult struct {
	Scenario         string  `json:"Scenario"`
	AccessMethod     string  `json:"AccessMethod"`
	CacheMode        string  `json:"CacheMode"`
	Scale            string  `json:"Scale"`
	HFRepo           string  `json:"HFRepo"`
	ShardCount       int     `json:"ShardCount"`
	TotalSizeGB      float64 `json:"TotalSizeGB"`
	TotalLoadTimeSec float64 `json:"TotalLoadTimeSec"`
	ThroughputMbps   float64 `json:"ThroughputMbps"`
	CacheHitRate     float64 `json:"CacheHitRate,omitempty"`
}

// SendPerfMetric sends a performance metric to EventHub for ingestion into Kusto.
// Always prints the metric to stdout for visibility.
// Only actually sends to EventHub if SEND_METRICS=true.
//
// The event is structured with fixed columns for indexing plus dynamic columns:
//
//	Kusto table schema:
//	.create table PerfMetrics (
//	    RunID: string,
//	    SuiteID: string,
//	    Timestamp: datetime,
//	    TestName: string,
//	    TestGitCommit: string,
//	    TestGitBranch: string,
//	    HelmChartVersion: string,
//	    HelmAppVersion: string,
//	    TestMetadata: dynamic,
//	    Result: dynamic
//	)
func SendPerfMetric(ctx context.Context, config EventHubConfig, metric *PerfMetric) error {
	// Set run metadata - RunID from pipeline environment variables
	pipelineRunID := strings.Trim(fmt.Sprintf("%s-%s", buildDefinitionName, buildNum), "-")
	if pipelineRunID != "" {
		metric.RunID = pipelineRunID
	}
	metric.Timestamp = time.Now().Format(time.RFC3339)

	// Pretty print the metric for visibility
	prettyJSON, _ := json.MarshalIndent(metric, "", "  ")
	log.Info("Perf metric", "metric", string(prettyJSON))

	// Only send if metrics are enabled
	if SkipSendMetrics {
		log.Info("SEND_METRICS not set to 'true', skipping EventHub send")
		return nil
	}
	if config.Namespace == "" || config.Name == "" {
		return fmt.Errorf("EVENTHUB_NAMESPACE and EVENTHUB_NAME are required when SEND_METRICS=true")
	}

	// Convert struct to map for EventHub
	jsonBytes, err := json.Marshal(metric)
	if err != nil {
		return fmt.Errorf("failed to marshal metric: %w", err)
	}
	var event map[string]any
	if err := json.Unmarshal(jsonBytes, &event); err != nil {
		return fmt.Errorf("failed to convert metric to map: %w", err)
	}

	return eventhub.SendEvent(ctx, config.Namespace, config.Name, event)
}

// SendMetricsEnabled returns true if metrics should be sent to EventHub/Kusto.
// Check this before calling SendPerfMetric to avoid unnecessary work.
func SendMetricsEnabled() bool {
	return !SkipSendMetrics
}
