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
# Perf test specific flags
# --------------------------------------------------------------------------------------------------------------------
*/

package perf

import (
	"context"
	"flag"

	"github.com/Azure/dacs/test/pkg/flux"
)

// Performance test flags
type Options struct {
	// Common report/output flags
	*flux.Options

	// DataSizeGiB is the data size in GiB for perf tests
	DataSizeGiB int
	// CacheServerCount is the number of cache servers to use
	CacheServerCount int
	// NumThreads is the number of threads for perf client
	NumThreads int
	// ChecksumAlgorithm is the checksum algorithm (xxhash32 or crc32)
	ChecksumAlgorithm string

	// StorageContainer is the name of the Azure Storage Container
	StorageContainer string
	// BlobCount is the number of blobs to write/read per client
	BlobCount uint
	// BlobSize is the size of each blob in bytes
	BlobSize uint64
	// WriteMode is the write mode (write_cache_only, write_blob_only, write_cache_blob_async, write_sync)
	WriteMode string
	// ClientCount is the number of cache clients (0 = auto-detect from nodes in cluster)
	ClientCount uint
	// NumProcesses is the number of parallel processes for adapter benchmark
	NumProcesses int

	// Scenario test flags
	// ScenarioScale selects the model scale for scenario tests (1B, 7B, 13B, 70B)
	ScenarioScale string
	// ScenarioMethods is a comma-separated list of access methods to test
	ScenarioMethods string
	// ScenarioCacheModes is a comma-separated list of cache modes to test
	ScenarioCacheModes string
	// RunAIStreamerConcurrency is the number of concurrent streams for Run:AI streamer
	RunAIStreamerConcurrency int
	// RunAIStreamerBlockSize is the read block size for Run:AI streamer
	RunAIStreamerBlockSize int
}

// AddFlags adds performance test specific flags to the provided FlagSet
func AddFlags(fs *flag.FlagSet, opts *Options) error {
	var commonOpts flux.Options
	if err := flux.AddFlags(fs, &commonOpts); err != nil {
		return err
	}
	opts.Options = &commonOpts

	// Perfclient specific flags
	fs.IntVar(&opts.DataSizeGiB, "data-size-gib", 1, "Data size in GiB for perf tests")
	fs.IntVar(&opts.CacheServerCount, "cache-server-count", 1, "Number of cache servers to use")
	fs.IntVar(&opts.NumThreads, "num-threads", 1, "Number of threads for perf client")
	fs.StringVar(&opts.ChecksumAlgorithm, "checksum-algorithm", "xxhash32", "Checksum algorithm (xxhash32 or crc32)")

	// DACS client specific flags
	fs.StringVar(&opts.StorageContainer, "storage-container", "test-data", "Azure Storage Container name")
	fs.UintVar(&opts.BlobCount, "blob-count", 50, "Number of blobs to write/read per client")
	fs.Uint64Var(&opts.BlobSize, "blob-size", 1073741824, "Size of each blob in bytes (1GB default)")
	fs.StringVar(&opts.WriteMode, "write-mode", "write_sync", "Write mode (write_cache_only, write_blob_only, write_cache_blob_async, write_sync)")
	fs.UintVar(&opts.ClientCount, "count", 0, "Number of cache clients (0 = auto-detect from nodes)")
	fs.IntVar(&opts.NumProcesses, "num-processes", 1, "Number of parallel processes for adapter benchmark")

	// Scenario test flags
	fs.StringVar(&opts.ScenarioScale, "scenario-scale", "7B", "Model scale for scenario tests (1B, 7B, 13B, 70B)")
	fs.StringVar(&opts.ScenarioMethods, "scenario-methods", "blobfile", "Comma-separated access methods (python,blobfile,boostedblob,interceptor,stream_client,huggingface,runaistreamer,blobfuse)")
	fs.StringVar(&opts.ScenarioCacheModes, "scenario-cache-modes", "no_cache", "Comma-separated cache modes (no_cache,cold_cache,warm_cache)")
	fs.IntVar(&opts.RunAIStreamerConcurrency, "runai-streamer-concurrency", 16, "Number of concurrent streams for Run:AI streamer")
	fs.IntVar(&opts.RunAIStreamerBlockSize, "runai-streamer-block-size", 33554432, "Read block size in bytes for Run:AI streamer (default 32MB, matches cache chunk size)")

	return nil
}

// ResolveDefaults infers values for flags that were not explicitly set.
func (o *Options) ResolveDefaults(ctx context.Context) {
	o.Options.ResolveDefaults(ctx)
}
