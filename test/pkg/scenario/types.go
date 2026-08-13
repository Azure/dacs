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
# Workload scenario performance test types and configuration
# --------------------------------------------------------------------------------------------------------------------
*/
package scenario

import (
	"fmt"
)

// CacheMode represents the cache state for a test run.
type CacheMode string

const (
	NoCache   CacheMode = "no_cache"   // Direct blob access, cache bypassed
	ColdCache CacheMode = "cold_cache" // First load, cache miss → populate
	WarmCache CacheMode = "warm_cache" // Second load, cache hit
)

// AccessMethod represents the storage access method under test.
type AccessMethod string

const (
	PythonClient  AccessMethod = "python"        // py_dacs_client
	Blobfile      AccessMethod = "blobfile"      // Azure blobfile SDK
	BoostedBlob   AccessMethod = "boostedblob"   // Boostedblob-compatible async API via DACSCacheClient
	Interceptor   AccessMethod = "interceptor"   // LD_PRELOAD storage_intercept
	RunAIStreamer AccessMethod = "runaistreamer" // Run:AI Model Streamer
	HFDownloader  AccessMethod = "huggingface"   // Hugging Face hub download API
	StreamClient  AccessMethod = "stream_client" // Native C++ DACS stream client
)

// ScaleProfile defines a model scale for testing, referencing a real HF model.
// Shard count and sizes are discovered at runtime from the blob manifest.
type ScaleProfile struct {
	Name       string // e.g., "7B-14GB"
	HFRepo     string // Hugging Face repo ID (source of truth)
	HFRevision string // HF revision/branch (default: "main")
}

// BlobPrefix returns the blob storage path prefix, derived from the HF repo ID.
func (s ScaleProfile) BlobPrefix() string {
	return "scenario-models/" + s.HFRepo
}

// CacheGroupID returns the deterministic cache group ID for this scale profile.
func (s ScaleProfile) CacheGroupID() string {
	return "scenario:" + s.HFRepo
}

// Predefined scale profiles using public Hugging Face models.
var (
	Scale1B  = ScaleProfile{Name: "1B-3GB", HFRepo: "microsoft/phi-1_5"}
	Scale7B  = ScaleProfile{Name: "7B-14GB", HFRepo: "mistralai/Mistral-7B-v0.1"}
	Scale13B = ScaleProfile{Name: "13B-26GB", HFRepo: "NousResearch/Llama-2-13b-hf"}
	Scale70B = ScaleProfile{Name: "70B-140GB", HFRepo: "NousResearch/Llama-2-70b-hf"}
)

// ScaleByName returns a ScaleProfile by its short name (e.g., "1B", "7B").
func ScaleByName(name string) (ScaleProfile, error) {
	switch name {
	case "1B":
		return Scale1B, nil
	case "7B":
		return Scale7B, nil
	case "13B":
		return Scale13B, nil
	case "70B":
		return Scale70B, nil
	default:
		return ScaleProfile{}, fmt.Errorf("unknown scale: %s (valid: 1B, 7B, 13B, 70B)", name)
	}
}

// ModelLoadingResult holds the metrics from a model loading scenario run.
type ModelLoadingResult struct {
	// Scenario identification
	Scenario     string `json:"Scenario"`     // "model_loading"
	AccessMethod string `json:"AccessMethod"` // e.g., "python"
	CacheMode    string `json:"CacheMode"`    // "no_cache", "cold_cache", "warm_cache"
	Scale        string `json:"Scale"`        // e.g., "7B-14GB"

	// Model details (discovered at runtime)
	HFRepo      string  `json:"HFRepo"`
	ShardCount  int     `json:"ShardCount"`
	TotalSizeGB float64 `json:"TotalSizeGB"`

	// Timing metrics
	TotalLoadTimeSec    float64 `json:"TotalLoadTimeSec"`
	TimeToFirstByteSec  float64 `json:"TimeToFirstByteSec"`
	TimeToFirstShardSec float64 `json:"TimeToFirstShardSec"`

	// Throughput
	ThroughputMbps float64 `json:"ThroughputMbps"`

	// Per-shard latency
	ShardLoadTimes []float64 `json:"ShardLoadTimes,omitempty"` // Individual shard load times

	// Resource metrics
	PeakMemoryMB float64 `json:"PeakMemoryMB,omitempty"`

	// Cache metrics (zero if no-cache mode)
	CacheHitRate float64 `json:"CacheHitRate,omitempty"`
}

// ModelLoadingConfig holds configuration for a model loading test run.
type ModelLoadingConfig struct {
	Namespace        string
	StorageAccount   string
	StorageContainer string
	TestImage        string

	// Authentication
	WorkloadIdentityClientID string
	WorkloadIdentitySA       string

	// Cache configuration
	CacheDiscoveryURL string

	// Run:AI Model Streamer configuration
	RunAIStreamerConcurrency int // Number of concurrent streams (default: 16)
	RunAIStreamerBlockSize   int // Read block size in bytes (default: 32MB, matches cache chunk size)
}
