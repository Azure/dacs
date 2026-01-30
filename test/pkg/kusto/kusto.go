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
	Name:      "intpipeline",
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

// PerfResult contains the cache server performance test results (dynamic column in Kusto).
type PerfResult struct {
	Mode                    string `json:"Mode"`
	DurationMs              int64  `json:"DurationMs"`
	TotalBytesTransferred   int64  `json:"TotalBytesTransferred"`
	ThroughputMbps          int64  `json:"ThroughputMbps"`
	TotalFailures           int    `json:"TotalFailures"`
	ExpectedChunks          int    `json:"ExpectedChunks"`
	SuccessfulChunks        int    `json:"SuccessfulChunks"`
	NumThreads              int    `json:"NumThreads"`
	DataSizeGiB             int    `json:"DataSizeGiB"`
	NumIterations           int    `json:"NumIterations"`
	NumServers              int    `json:"NumServers"`
	EnableCalculateChecksum bool   `json:"EnableCalculateChecksum"`
	EnableValidateChecksum  bool   `json:"EnableValidateChecksum"`
	ChecksumAlgorithm       string `json:"ChecksumAlgorithm"`
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
