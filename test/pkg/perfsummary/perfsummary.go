// Package perfsummary provides utilities for parsing and handling
// performance test summary output from the PerfClient.
package perfsummary

import (
	"fmt"
	"regexp"
	"strconv"
)

// Summary contains the parsed performance test summary from the perf client logs.
type Summary struct {
	Mode                          string `json:"mode"`
	StartTimestamp                int64  `json:"startTimestamp"`
	EndTimestamp                  int64  `json:"endTimestamp"`
	DurationMs                    int64  `json:"durationMs"`
	TotalBytesTransferred         int64  `json:"totalBytesTransferred"`
	ThroughputMbps                int64  `json:"throughputMbps"`
	TotalIndividualFailures       int    `json:"totalIndividualFailures"`
	ExpectedChunksToTransfer      int    `json:"expectedChunksToTransfer"`
	ChunksSuccessfullyTransferred int    `json:"chunksSuccessfullyTransferred"`
}

// String returns a formatted string representation of the Summary.
func (p Summary) String() string {
	return fmt.Sprintf(
		"Mode: %s, Duration: %dms, Bytes: %d, Throughput: %d Mbps, Failures: %d, Chunks: %d/%d",
		p.Mode, p.DurationMs, p.TotalBytesTransferred, p.ThroughputMbps,
		p.TotalIndividualFailures, p.ChunksSuccessfullyTransferred, p.ExpectedChunksToTransfer,
	)
}

// summaryRegex is the compiled regex for parsing perf client summary logs.
// Example log line:
// DACS_CTR:INFO|StorageCommon|...|PerfClient.cpp:RunPerfTest(214): Summary: mode upload,start_timestamp 1766178905344,...
var summaryRegex = regexp.MustCompile(`Summary: mode (\w+),start_timestamp (\d+),end_timestamp (\d+),duration_ms (\d+),total_bytes_transferred (\d+),throughput_Mbps (\d+),total_individual_failures (\d+),expected_chunks_to_transfer (\d+),chunks_successfully_transferred (\d+)`)

// Parse parses the performance summary from the perf client log output.
func Parse(logs string) (*Summary, error) {
	matches := summaryRegex.FindStringSubmatch(logs)
	if matches == nil {
		return nil, fmt.Errorf("could not find Summary line in logs")
	}

	// Parse all the numeric fields
	startTs, err := strconv.ParseInt(matches[2], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse start_timestamp: %w", err)
	}
	endTs, err := strconv.ParseInt(matches[3], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse end_timestamp: %w", err)
	}
	durationMs, err := strconv.ParseInt(matches[4], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse duration_ms: %w", err)
	}
	totalBytes, err := strconv.ParseInt(matches[5], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse total_bytes_transferred: %w", err)
	}
	throughput, err := strconv.ParseInt(matches[6], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse throughput_Mbps: %w", err)
	}
	failures, err := strconv.Atoi(matches[7])
	if err != nil {
		return nil, fmt.Errorf("failed to parse total_individual_failures: %w", err)
	}
	expectedChunks, err := strconv.Atoi(matches[8])
	if err != nil {
		return nil, fmt.Errorf("failed to parse expected_chunks_to_transfer: %w", err)
	}
	successChunks, err := strconv.Atoi(matches[9])
	if err != nil {
		return nil, fmt.Errorf("failed to parse chunks_successfully_transferred: %w", err)
	}

	return &Summary{
		Mode:                          matches[1],
		StartTimestamp:                startTs,
		EndTimestamp:                  endTs,
		DurationMs:                    durationMs,
		TotalBytesTransferred:         totalBytes,
		ThroughputMbps:                throughput,
		TotalIndividualFailures:       failures,
		ExpectedChunksToTransfer:      expectedChunks,
		ChunksSuccessfullyTransferred: successChunks,
	}, nil
}
