/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package v1

const (
	DefaultPort                         = int32(9065)
	DefaultMetricsPort                  = int32(9096)
	DefaultNumServers                   = int32(1)
	DefaultNodeSelectorKey              = "dacs.azure.com/processing-unit"
	DefaultNodeSelectorValue            = "gpu"
	DefaultInfinibandResourceLimitName  = "rdma/hca_rdma_infiniband"
	DefaultInfinibandResourceLimitCount = int32(0)
	DefaultEnableAccessTimeEviction     = true
	DefaultEvictionMaintainFreeSpace    = true
	DefaultLastAccessThreshold          = "30d"
	DefaultSizeThreshold                = "90"
	DefaultPassFrequency                = "1h"
	DefaultSizeTarget                   = "80"
	DefaultNodeCacheFolderPath          = "/var/lib/ssd/cacheserver"
)
