///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <string>
#include <vector>

#include "CacheClient.h"
#include "ConnectionManager.h"
//---------------------------------------------------------------------------------------------------------------------
class CacheClientTests;
//---------------------------------------------------------------------------------------------------------------------
class CacheClientImpl
{
  public:
    CacheClientImpl();

    CacheClientStatus Initialize(
        /*[in]*/ const char* const* arrServers,
        /*[in]*/ uint32_t nNumServers,
        /*[in]*/ uint32_t nPort,
        /*[in]*/ bool bEnableIB,
        /*[in]*/ uint32_t nConnectionPoolSize,
        /*[in]*/ bool bEnableAuth,
        /*[in]*/ const char* strAccountName,
        /*[in]*/ const char* strAccountKey,
        /*[inopt]*/ bool bUseLocalServer = false,
        /*[inopt]*/ bool bReuseConnections = true,
        /*[in]*/ std::size_t nMaxBufferSizeBytes = MAX_BUFFER_SIZE_BYTES);

    CacheClientStatus Upload(
        /*[in]*/ const char* strCacheKey,
        /*[in]*/ const void* pBuffer,
        /*[in]*/ const std::size_t nBufferLength,
        /*[in]*/ const uint32_t nExpirySeconds,
        /*[in]*/ const char* strGroupId,
        /*[in]*/ const Metadata* pMetadata,
        /*[in]*/ const char* strEncryptionKey,
        /*[in]*/ bool bEnableLock = true,
        /*[inopt]*/ const char* strCorrelationId = nullptr);

    CacheClientStatus Download(
        /*[in]*/ const char* strCacheKey,
        /*[out]*/ void* outBuffer,
        /*[inout]*/ std::size_t* nLength,
        /*[out]*/ Metadata* pMetadata,
        /*[in]*/ const char* strEncryptionKey,
        /*[inopt]*/ std::size_t nStartOffset = 0, // Default to download from the start
        /*[inopt]*/ bool bEnableLock = true,
        /*[inopt]*/ const char* strCorrelationId = nullptr);

    CacheClientStatus DeleteGroup(/*[in]*/ const char* groupID,
                                  /*[inopt]*/ const char* strCorrelationId = nullptr);

    bool IsInitialized() const;

  private:
    std::atomic<bool> m_bInitialized;
    std::mutex m_mtxInitialization;
    std::vector<std::string> m_vecServers;
    uint32_t m_nNumServers;
    uint32_t m_nPort;
    bool m_bEnableIB;
    bool m_bEnableAuth;
    std::string m_strAccountName;
    std::string m_strAccountKey;
    uint32_t m_nConnectionPoolSize;
    std::unique_ptr<ConnectionManager> m_spConnectionManager;
    static std::size_t nDefaultConnectionPoolSize;
    size_t m_nMaxBufferSizeBytes;

    // Friends for testing
    FRIEND_TEST(CacheClientTests, ForceReuseConnections);
    FRIEND_TEST(CacheClientTests, Initialization);
};
//---------------------------------------------------------------------------------------------------------------------
