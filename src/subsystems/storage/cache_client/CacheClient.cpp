///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include <iostream>
#include "CacheClient.h"
//---------------------------------------------------------------------------------------------------------------------
extern "C" CacheClientStatus Initialize(
    /*[in]*/ const char* const* arrServers,
    /*[in]*/ uint32_t nServers,
    /*[in]*/ uint32_t nPort,
    /*[in]*/ bool bEnableIB,
    /*[in]*/ const AuthCredentials* pAuthCredentials)
{
    return CacheClientStatus::SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" CacheClientStatus Upload(
    /*[in]*/ const char* strCacheKey,
    /*[in]*/ const void* pBuffer,
    /*[in]*/ const uint64_t nLength,
    /*[inopt]*/ const uint32_t nExpirySeconds,
    /*[inopt]*/ const char* strGroupId,
    /*[inopt]*/ const Metadata* pMetadata,
    /*[inopt]*/ const char* strEncryptionKey)
{
    return CacheClientStatus::SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" CacheClientStatus Download(
    /*[in]    */ const char* strCacheKey,
    /*[out]   */ void* pBuffer,
    /*[inout] */ uint64_t* nLength,
    /*[outopt]*/ Metadata* pMetadata,
    /*[inopt] */ const char* strEncryptionKey)
{
    return CacheClientStatus::SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
extern "C" CacheClientStatus DeleteGroup(/*[in]*/ const char* strGroupId)
{
    return CacheClientStatus::SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------