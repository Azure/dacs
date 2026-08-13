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

package v1

const (
	// Suffixes appended to the Cache name to form derived resource names.
	DiscoverySuffix       = "-discovery"
	DiscoveryConfigSuffix = "-discovery-config"

	// MaxCacheNameLength is the maximum allowed length for a Cache resource
	// name. The controller creates derived resources by appending suffixes to
	// the name (longest is DiscoverySuffix at 10 chars). Service names must
	// be valid DNS labels (max 63 chars), so the Cache name is limited to
	// 63 - len(DiscoverySuffix) = 53.
	MaxCacheNameLength = 63 - len(DiscoverySuffix)

	// RandomNameSuffixLength is the length of the random suffix that the
	// Kubernetes apiserver appends to metadata.generateName to form the final
	// metadata.name. See k8s.io/apiserver/pkg/storage/names (randomLength).
	RandomNameSuffixLength = 5

	// MaxCacheGenerateNameLength is the maximum allowed length for a Cache
	// metadata.generateName prefix. The apiserver appends RandomNameSuffixLength
	// characters to form the final name, which must not exceed MaxCacheNameLength.
	MaxCacheGenerateNameLength = MaxCacheNameLength - RandomNameSuffixLength

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
	DefaultPassFrequency                = "15m"
	DefaultSizeTarget                   = "80"
	DefaultBucketInterval               = "15m"
	DefaultNodeCacheFolderPath          = "/var/lib/ssd/cacheserver"
)
