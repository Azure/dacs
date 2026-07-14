///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheClient.h"

#include <memory>
#include <stdint.h>

#include "CacheClientImpl.h"
#include "CacheClientLogger.h"
//---------------------------------------------------------------------------------------------------------------------
static std::shared_ptr<CacheClientImpl> GetImplInstance()
{
    static std::shared_ptr<CacheClientImpl> instance = std::make_shared<CacheClientImpl>();
    return instance;
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" CacheClientStatus Initialize(
    /*[in]*/ const char* const* arrServers,
    /*[in]*/ uint32_t nServers,
    /*[in]*/ uint32_t nPort,
    /*[in]*/ bool bEnableIB,
    /*[in]*/ bool bEnableCalculateChecksum,
    /*[in]*/ bool bEnableValidateChecksum,
    /*[in]*/ const char* strChecksumAlgorithm,
    /*[in]*/ const AuthCredentials* pAuthCredentials,
    /*[in]*/ bool bEnableConsistentHashing,
    /*[in]*/ CacheClientRetryConfig retryConfig)
{
    return GetImplInstance()->Initialize(arrServers,
                                         nServers,
                                         nPort,
                                         bEnableIB,
                                         bEnableCalculateChecksum,
                                         bEnableValidateChecksum,
                                         strChecksumAlgorithm,
                                         0,                             /* nConnectionPoolSize */
                                         (pAuthCredentials != nullptr), /* bEnableAuth */
                                         pAuthCredentials ? pAuthCredentials->m_strAccountKey : nullptr,
                                         pAuthCredentials ? pAuthCredentials->m_strAccountName : nullptr,
                                         false, /* bUseLocalServer */
                                         true,  /* bReuseConnections */
                                         false, /* bCacheServerDiscoveryEnabled */
                                         300,   /* nCacheServerDiscoveryIntervalInSeconds */
                                         "127.0.0.1", /* nCacheServerDiscoveryEndpoint */
                                         bEnableConsistentHashing,
                                         retryConfig);
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