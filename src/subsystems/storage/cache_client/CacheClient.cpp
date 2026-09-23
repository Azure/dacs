/// Copyright (c) Microsoft Corporation.
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

#include "CacheClient.h"

#include <memory>
#include <stdint.h>

#include "CacheClientImpl.h"
#include "CacheClientLogger.h"
//---------------------------------------------------------------------------------------------------------------------
// The discovery endpoint reaches ConnectionManager as a `const std::string&` (see
// ConnectionManager's constructor), so it must be given a valid pointer even when discovery is off
// and the value is never read. This is that filler; it is only ever passed when
// bCacheServerDiscoveryEnabled is false.
static const char* const c_strUnusedDiscoveryEndpoint = "127.0.0.1";
//---------------------------------------------------------------------------------------------------------------------
static std::shared_ptr<CacheClientImpl> GetImplInstance()
{
    static std::shared_ptr<CacheClientImpl> instance = std::make_shared<CacheClientImpl>();
    return instance;
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" CacheClientStatus Initialize(
    /*[inopt]*/ const char* const* arrServers,
    /*[in]   */ uint32_t nServers,
    /*[in]   */ uint32_t nPort,
    /*[in]   */ bool bEnableIB,
    /*[in]   */ bool bEnableCalculateChecksum,
    /*[in]   */ bool bEnableValidateChecksum,
    /*[in]   */ const char* strChecksumAlgorithm,
    /*[in]   */ const AuthCredentials* pAuthCredentials,
    /*[in]   */ bool bEnableConsistentHashing,
    /*[in]   */ CacheClientRetryConfig retryConfig,
    /*[in]   */ bool bEnableCacheServerDiscovery,
    /*[in]   */ uint32_t nCacheServerDiscoveryIntervalInSeconds,
    /*[inopt]*/ const char* strCacheServerDiscoveryEndpoint,
    /*[in]   */ ChecksumMetadataPolicy checksumMetadataPolicy)
{
    // Cache authentication has been removed. Credentials are no longer supported; reject
    // any caller that still supplies them rather than silently ignoring a security setting.
    if (pAuthCredentials != nullptr)
    {
        LOG_ERROR("Initialize called with non-null AuthCredentials, but cache authentication is no longer supported");
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    // Discovery cannot work without an endpoint to query. Reject it here rather than substituting a
    // placeholder: a placeholder would satisfy the equivalent check inside CacheClientImpl, so the
    // caller would get SUCCESS, discover against the placeholder forever and never learn that the
    // ring is empty. Every cache operation would then fail while the client still reports healthy.
    if (bEnableCacheServerDiscovery &&
        (strCacheServerDiscoveryEndpoint == nullptr || *strCacheServerDiscoveryEndpoint == '\0'))
    {
        LOG_ERROR("Initialize called with cache server discovery enabled but no discovery endpoint");
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    return GetImplInstance()->Initialize(arrServers,
                                         nServers,
                                         nPort,
                                         bEnableIB,
                                         bEnableCalculateChecksum,
                                         bEnableValidateChecksum,
                                         strChecksumAlgorithm,
                                         0,     /* nConnectionPoolSize */
                                         false, /* bUseLocalServer */
                                         true,  /* bReuseConnections */
                                         bEnableCacheServerDiscovery,
                                         nCacheServerDiscoveryIntervalInSeconds,
                                         // The endpoint is only meaningful when discovery is on, and the
                                         // guard above guarantees it is present in that case. When discovery
                                         // is off the value is unused, so substitute a placeholder rather
                                         // than construct a std::string from the NULL the caller may pass.
                                         bEnableCacheServerDiscovery ? strCacheServerDiscoveryEndpoint
                                                                     : c_strUnusedDiscoveryEndpoint,
                                         bEnableConsistentHashing,
                                         retryConfig,
                                         checksumMetadataPolicy);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" CacheClientStatus Upload(
    /*[in]*/ const char* strCacheKey,
    /*[in]*/ const void* pBuffer,
    /*[in]*/ const uint64_t nLength,
    /*[in]*/ const uint32_t nExpirySeconds,
    /*[in]*/ const char* strGroupId,
    /*[in]*/ const Metadata* pMetadata,
    /*[in]*/ const char* strEncryptionKey)
{
    return GetImplInstance()->Upload(
        strCacheKey, pBuffer, nLength, nExpirySeconds, strGroupId, pMetadata, strEncryptionKey);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" CacheClientStatus Download(
    /*[in]*/ const char* strCacheKey,
    /*[out]*/ void* pBuffer,
    /*[inout]*/ uint64_t* nLength,
    /*[outopt]*/ Metadata* pMetadata,
    /*[inopt]*/ const char* strEncryptionKey)
{
    return GetImplInstance()->Download(strCacheKey, pBuffer, nLength, pMetadata, strEncryptionKey);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" CacheClientStatus DeleteGroup(/*[in]*/ const char* strGroupId)
{
    return GetImplInstance()->DeleteGroup(strGroupId);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" CacheClientStatus GetLastAccessTimeDistribution(/*[in]*/ uint64_t nDistributionBucketSizeInHours,
                                                       /*[out]*/ LastAccessTimeDistribution* pLastAccessTimeDistribution)
{
    return GetImplInstance()->GetLastAccessTimeDistribution(nDistributionBucketSizeInHours, pLastAccessTimeDistribution);
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" CacheClientStatus DeleteByLastAccessTime(/*[in]*/ const char* strLastAccessTime)
{
    return GetImplInstance()->DeleteByLastAccessTime(strLastAccessTime);
}
//---------------------------------------------------------------------------------------------------------------------