/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Perf test specific flags
# --------------------------------------------------------------------------------------------------------------------
*/

package perf

import (
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

	// DACS client test flags
	// ClientImage is the DACS Cache Client Image to use for testing
	ClientImage string
	// StorageAccount is the name of the Azure Storage Account
	StorageAccount string
	// BlobCount is the number of blobs to write/read per client
	BlobCount uint
	// BlobSize is the size of each blob in bytes
	BlobSize uint64
	// WriteMode is the write mode (write_cache_only, write_blob_only, write_cache_blob_async, write_sync)
	WriteMode string
	// ClientCount is the number of cache clients (0 = auto-detect from nodes in cluster)
	ClientCount uint
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
	fs.StringVar(&opts.ClientImage, "client-image", "", "DACS Cache Client Image to use for testing")
	fs.StringVar(&opts.StorageAccount, "storage-account", "", "Azure Storage Account name")
	fs.UintVar(&opts.BlobCount, "blob-count", 50, "Number of blobs to write/read per client")
	fs.Uint64Var(&opts.BlobSize, "blob-size", 1073741824, "Size of each blob in bytes (1GB default)")
	fs.StringVar(&opts.WriteMode, "write-mode", "write_sync", "Write mode (write_cache_only, write_blob_only, write_cache_blob_async, write_sync)")
	fs.UintVar(&opts.ClientCount, "count", 0, "Number of cache clients (0 = auto-detect from nodes)")

	return nil
}
