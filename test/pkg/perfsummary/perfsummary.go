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

// Package perfsummary provides utilities for parsing and handling
// performance test summary output from the PerfClient.
package perfsummary

import (
	"encoding/json"
	"fmt"
	"regexp"
	"strconv"
)

// Summary contains the parsed performance test summary from the perf client logs.
type Summary struct {
	Mode                          string `json:"mode"`
	ChunkSizeBytes                int64  `json:"chunkSizeBytes"`
	StartTimestamp                int64  `json:"startTimestamp"`
	EndTimestamp                  int64  `json:"endTimestamp"`
	DurationMs                    int64  `json:"durationMs"`
	TotalBytesTransferred         int64  `json:"totalBytesTransferred"`
	ThroughputMbps                int64  `json:"throughputMbps"`
	TotalIndividualFailures       int    `json:"totalIndividualFailures"`
	ExpectedChunksToTransfer      int    `json:"expectedChunksToTransfer"`
	ChunksSuccessfullyTransferred int    `json:"chunksSuccessfullyTransferred"`
	PerChunkP50Ms                 int64  `json:"perchunkP50Ms"`
	PerChunkP90Ms                 int64  `json:"perchunkP90Ms"`
	PerChunkP99Ms                 int64  `json:"perchunkP99Ms"`
}

// String returns a formatted string representation of the Summary.
func (p Summary) String() string {
	return fmt.Sprintf(
		"Mode: %s, ChunkSize: %d bytes, Duration: %dms, Bytes: %d, Throughput: %d Mbps, Failures: %d, Chunks: %d/%d, PerChunk p50/p90/p99: %d/%d/%d ms",
		p.Mode, p.ChunkSizeBytes, p.DurationMs, p.TotalBytesTransferred, p.ThroughputMbps,
		p.TotalIndividualFailures, p.ChunksSuccessfullyTransferred, p.ExpectedChunksToTransfer,
		p.PerChunkP50Ms, p.PerChunkP90Ms, p.PerChunkP99Ms,
	)
}

// summaryRegex is the compiled regex for parsing perf client summary logs.
// Example log line:
// DACS_CTR:INFO|StorageCommon|...|PerfClient.cpp:RunPerfTest(214): Summary: mode upload,chunksize_bytes 33554432,start_timestamp 1766178905344,...
var summaryRegex = regexp.MustCompile(`Summary: mode (\w+),chunksize_bytes (\d+),start_timestamp (\d+),end_timestamp (\d+),duration_ms (\d+),total_bytes_transferred (\d+),throughput_Mbps (\d+),total_individual_failures (\d+),expected_chunks_to_transfer (\d+),chunks_successfully_transferred (\d+),perchunk_p50_ms (\d+),perchunk_p90_ms (\d+),perchunk_p99_ms (\d+)`)

// Parse parses the performance summary from the perf client log output.
func Parse(logs string) (*Summary, error) {
	matches := summaryRegex.FindStringSubmatch(logs)
	if matches == nil {
		return nil, fmt.Errorf("could not find Summary line in logs")
	}

	// Parse all the numeric fields
	chunkSizeBytes, err := strconv.ParseInt(matches[2], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse chunksize_bytes: %w", err)
	}
	startTs, err := strconv.ParseInt(matches[3], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse start_timestamp: %w", err)
	}
	endTs, err := strconv.ParseInt(matches[4], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse end_timestamp: %w", err)
	}
	durationMs, err := strconv.ParseInt(matches[5], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse duration_ms: %w", err)
	}
	totalBytes, err := strconv.ParseInt(matches[6], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse total_bytes_transferred: %w", err)
	}
	throughput, err := strconv.ParseInt(matches[7], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse throughput_Mbps: %w", err)
	}
	failures, err := strconv.Atoi(matches[8])
	if err != nil {
		return nil, fmt.Errorf("failed to parse total_individual_failures: %w", err)
	}
	expectedChunks, err := strconv.Atoi(matches[9])
	if err != nil {
		return nil, fmt.Errorf("failed to parse expected_chunks_to_transfer: %w", err)
	}
	successChunks, err := strconv.Atoi(matches[10])
	if err != nil {
		return nil, fmt.Errorf("failed to parse chunks_successfully_transferred: %w", err)
	}
	perChunkP50, err := strconv.ParseInt(matches[11], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse perchunk_p50_ms: %w", err)
	}
	perChunkP90, err := strconv.ParseInt(matches[12], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse perchunk_p90_ms: %w", err)
	}
	perChunkP99, err := strconv.ParseInt(matches[13], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("failed to parse perchunk_p99_ms: %w", err)
	}

	return &Summary{
		Mode:                          matches[1],
		ChunkSizeBytes:                chunkSizeBytes,
		StartTimestamp:                startTs,
		EndTimestamp:                  endTs,
		DurationMs:                    durationMs,
		TotalBytesTransferred:         totalBytes,
		ThroughputMbps:                throughput,
		TotalIndividualFailures:       failures,
		ExpectedChunksToTransfer:      expectedChunks,
		ChunksSuccessfullyTransferred: successChunks,
		PerChunkP50Ms:                 perChunkP50,
		PerChunkP90Ms:                 perChunkP90,
		PerChunkP99Ms:                 perChunkP99,
	}, nil

}

// DACS Client will log below summary:
//      DACS_Cache_Client_Test:Wrote 10 blobs with total 10737418240 bytes (10.0 GBs) in 11.24 seconds at 7.120259300233558 Gbps
//      DACS_Cache_Client_Test:Read 10737418240 bytes (10.0 GBs) in 7.85 seconds at 10.197416264362593 Gbps

type DACSClientPerfSummary struct {
	TotalBlobsWritten uint64 `json:"totalBlobsWritten"`

	// Write related metrics
	TotalBytesWritten     uint64  `json:"totalBytesWritten"`
	TotalWriteTimeSeconds float64 `json:"totalWriteTimeSeconds"`
	WriteThroughputGbps   float64 `json:"writeThroughputGbps"`

	// Read related metrics
	TotalBytesRead       uint64  `json:"totalBytesRead"`
	TotalReadTimeSeconds float64 `json:"totalReadTimeSeconds"`
	ReadThroughputGbps   float64 `json:"readThroughputGbps"`
}

// String returns a formatted string representation of the Summary.
func (p DACSClientPerfSummary) String() string {
	return fmt.Sprintf(
		"TotalBlobsWritten: %d, TotalBytesWritten: %d, TotalWriteTimeSeconds: %.2f, WriteThroughputGbps: %.2f, TotalBytesRead: %d, TotalReadTimeSeconds: %.2f, ReadThroughputGbps: %.2f",
		p.TotalBlobsWritten, p.TotalBytesWritten,
		p.TotalWriteTimeSeconds, p.WriteThroughputGbps,
		p.TotalBytesRead, p.TotalReadTimeSeconds, p.ReadThroughputGbps,
	)
}

func ParseDACSClientPerfSummary(logs string, writePerfRegx, readPerfRegx *regexp.Regexp) (*DACSClientPerfSummary, error) {
	// Implementation to parse logs and extract performance metrics
	matchesWrite := writePerfRegx.FindStringSubmatch(logs)
	if matchesWrite == nil {
		return nil, fmt.Errorf("failed to parse write performance metrics from logs")
	}

	// Extract write metrics from regex matches (If log order changes this needs to be updated)
	// Index Type
	// 1     blobs
	// 2     bytes
	// 3     seconds
	// 4     Gbps
	totalBlobs, err := strconv.ParseUint(matchesWrite[1], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("Invalid blobs written value: %v", err)
	}
	totalBytesWritten, err := strconv.ParseUint(matchesWrite[2], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("Invalid total bytes written value: %v", err)
	}
	writeSeconds, err := strconv.ParseFloat(matchesWrite[3], 64)
	if err != nil {
		return nil, fmt.Errorf("Invalid write time seconds value: %v", err)
	}
	writeGbps, err := strconv.ParseFloat(matchesWrite[4], 64)
	if err != nil {
		return nil, fmt.Errorf("Invalid write throughput Gbps value: %v", err)
	}

	matchesRead := readPerfRegx.FindStringSubmatch(logs)
	if matchesRead == nil {
		return nil, fmt.Errorf("Failed to parse read performance metrics from logs: %v", err)
	}

	// Extract read metrics from regex matches (If log order changes this needs to be updated)
	// Index Type
	// 1     bytes
	// 2     seconds
	// 3     Gbps
	totalBytesRead, err := strconv.ParseUint(matchesRead[1], 10, 64)
	if err != nil {
		return nil, fmt.Errorf("Invalid total bytes read value: %v", err)
	}
	readSeconds, err := strconv.ParseFloat(matchesRead[2], 64)
	if err != nil {
		return nil, fmt.Errorf("Invalid read time seconds value: %v", err)
	}
	readGbps, err := strconv.ParseFloat(matchesRead[3], 64)
	if err != nil {
		return nil, fmt.Errorf("Invalid read throughput Gbps value: %v", err)
	}

	summary := &DACSClientPerfSummary{
		TotalBlobsWritten:     totalBlobs,
		TotalBytesWritten:     totalBytesWritten,
		TotalWriteTimeSeconds: writeSeconds,
		WriteThroughputGbps:   writeGbps,

		TotalBytesRead:       totalBytesRead,
		TotalReadTimeSeconds: readSeconds,
		ReadThroughputGbps:   readGbps,
	}

	return summary, nil
}

// DACSAdapterBenchmarkResult holds performance results for a single client from the benchmark.
type DACSAdapterBenchmarkResult struct {
	WriteBytesTransferred     uint64  `json:"write_bytes"`
	WriteDurationSeconds      float64 `json:"write_duration_seconds"`
	WriteThroughputMBps       float64 `json:"write_mbps"`
	ReadBytesTransferred      uint64  `json:"read_bytes"`
	ReadDurationSeconds       float64 `json:"read_duration_seconds"`
	ReadThroughputMBps        float64 `json:"read_mbps"`
	OverwriteBytesTransferred uint64  `json:"overwrite_bytes"`
	OverwriteDurationSeconds  float64 `json:"overwrite_duration_seconds"`
	OverwriteThroughputMBps   float64 `json:"overwrite_mbps"`
}

// DACSAdapterBenchmarkConfig holds the benchmark configuration.
type DACSAdapterBenchmarkConfig struct {
	StorageAccount string  `json:"storage_account"`
	Container      string  `json:"container"`
	BlobSizeGB     float64 `json:"blob_size_gb"`
	NumBlobs       int     `json:"num_blobs"`
	NumProcesses   int     `json:"num_processes"`
	WriteMode      string  `json:"write_mode"`
}

// DACSAdapterBenchmarkSummary holds the complete benchmark output.
type DACSAdapterBenchmarkSummary struct {
	Timestamp string                                   `json:"timestamp"`
	Config    DACSAdapterBenchmarkConfig            `json:"config"`
	Results   map[string]DACSAdapterBenchmarkResult `json:"results"`
}

// String returns a formatted string representation of the Summary.
func (s DACSAdapterBenchmarkSummary) String() string {
	var result string
	for client, r := range s.Results {
		result += fmt.Sprintf(
			"[%s] Write: %.2f GB in %.2fs (%.0f MB/s), Read: %.2f GB in %.2fs (%.0f MB/s)",
			client,
			float64(r.WriteBytesTransferred)/(1024*1024*1024), r.WriteDurationSeconds, r.WriteThroughputMBps,
			float64(r.ReadBytesTransferred)/(1024*1024*1024), r.ReadDurationSeconds, r.ReadThroughputMBps,
		)
		if r.OverwriteBytesTransferred > 0 {
			result += fmt.Sprintf(
				", Overwrite: %.2f GB in %.2fs (%.0f MB/s)",
				float64(r.OverwriteBytesTransferred)/(1024*1024*1024), r.OverwriteDurationSeconds, r.OverwriteThroughputMBps,
			)
		}
		result += "\n"
	}
	return result
}

// ParseDACSAdapterBenchmark parses the JSON output from throughput_benchmark.py.
func ParseDACSAdapterBenchmark(jsonData string) (*DACSAdapterBenchmarkSummary, error) {
	var summary DACSAdapterBenchmarkSummary
	if err := json.Unmarshal([]byte(jsonData), &summary); err != nil {
		return nil, fmt.Errorf("failed to parse benchmark JSON: %w", err)
	}
	return &summary, nil
}
