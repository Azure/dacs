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

#pragma once
#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <string>

#include "CacheClient.h"
#include "ConnectionManager.h"
#include "ChecksumFactory.h"
//---------------------------------------------------------------------------------------------------------------------
class CacheClientTests;
class ConsistentHashingTests;
//---------------------------------------------------------------------------------------------------------------------
class CacheClientImpl
{
  public:
    CacheClientImpl();

    CacheClientStatus Initialize(
        /*[in]*/ const char* const* arrServers,
        /*[in]*/ const uint32_t nNumServers,
        /*[in]*/ const uint32_t nPort,
        /*[in]*/ const bool bEnableIB,
        /*[in]*/ const bool bEnableCalculateChecksum,
        /*[in]*/ const bool bEnableValidateChecksum,
        /*[in]*/ const char* strChecksumAlgorithm,
        /*[in]*/ const uint32_t nConnectionPoolSize,
        /*[in]*/ const bool bEnableAuth,
        /*[in]*/ const char* strAccountName,
        /*[in]*/ const char* strAccountKey,
        /*[in]*/ const bool bUseLocalServer,
        /*[in]*/ const bool bReuseConnections,
        /*[in]*/ const bool bIsCacheServerDiscoveryEnabled,
        /*[in]*/ const uint32_t nCacheServerDiscoveryIntervalInSeconds,
        /*[in]*/ const char* nCacheServerDiscoveryEndpoint,
        /*[in]*/ const bool bEnableConsistentHashing,
        /*[in]*/ CacheClientRetryConfig retryConfig,
        /*[inopt]*/ const std::size_t nMaxBufferSizeBytes = MAX_BUFFER_SIZE_BYTES,
        /*[inopt]*/ const int nSocketSndbufSize = DEFAULT_SOCKET_BUF_SIZE,
        /*[inopt]*/ const int nSocketRcvbufSize = DEFAULT_SOCKET_BUF_SIZE);

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

    CacheClientStatus GetLastAccessTimeDistribution(
        /*[in]*/ uint64_t nDistributionBucketSizeInHours,
        /*[out]*/ LastAccessTimeDistribution* pLastAccessTimeDistribution,
        /*[inopt]*/ const char* strCorrelationId = nullptr);

    CacheClientStatus DeleteByLastAccessTime(/*[in]*/ const char* lastAccessTime,
                                  /*[inopt]*/ const char* strCorrelationId = nullptr);

    bool IsInitialized() const;

  private:
    const char* const CACHE_META_CHUNK_CHECKSUM_KEY = "CHUNK_CHECKSUM";
    const char* const CACHE_META_CHECKSUM_ALGORITHM_KEY = "CHECKSUM_ALGORITHM";
    static constexpr ChecksumAlgorithm DEFAULT_CHECKSUM_ALGORITHM = ChecksumAlgorithm::XXHASH32;
    
    std::atomic<bool> m_bInitialized;
    std::mutex m_mtxInitialization;
    bool m_bEnableIB;
    bool m_bEnableAuth;
    bool m_bEnableCalculateChecksum;
    bool m_bEnableValidateChecksum;
    std::string m_strAccountName;
    std::string m_strAccountKey;
    ChecksumAlgorithm m_checksumAlgorithm;
    uint32_t m_nConnectionPoolSize;
    CacheClientRetryConfig m_retryConfig;
    std::unique_ptr<ConnectionManager> m_spConnectionManager;
    static std::size_t nDefaultConnectionPoolSize;
    size_t m_nMaxBufferSizeBytes;

    // Delete a group from a single cache server
    CacheClientStatus DeleteGroupFromServer(
        /*[in]*/ const std::string& hostname,
        /*[in]*/ const char* groupID,
        /*[in]*/ const char* strCorrelationId);

    // Friends for testing
    friend class CacheClientTests;
    friend class ConsistentHashingTests;
    FRIEND_TEST(CacheClientTests, ForceReuseConnections);
    FRIEND_TEST(CacheClientTests, Initialization);
    FRIEND_TEST(CacheClientTests, RefreshCacheServersWithDiscoveryEndpoint);
    FRIEND_TEST(CacheClientTests, DiscoveryEndpointConfigurationValidation);
    FRIEND_TEST(CacheClientTests, ResilientInitializationWithNetworkFailures);
    FRIEND_TEST(CacheClientTests, DiscoveryResilience);
    
    // Checksum validation test friends
    FRIEND_TEST(CacheClientTests, ChecksumValidationWithXXH32Default);
    FRIEND_TEST(CacheClientTests, ChecksumValidationSkippedWhenMissingMetadata);
    FRIEND_TEST(CacheClientTests, DownloadFileWithChecksumMismatch);
    FRIEND_TEST(CacheClientTests, ChecksumValidationDetectsBitFlipCorruptionXXH32);
    FRIEND_TEST(CacheClientTests, ChecksumValidationWithCRC32Algorithm);
    FRIEND_TEST(CacheClientTests, ChecksumValidationDetectsAlgorithmMismatch);
    FRIEND_TEST(CacheClientTests, ChecksumValidationEdgeCases);

    FRIEND_TEST(ConsistentHashingTests, ServerSelectionConsistency);
    FRIEND_TEST(ConsistentHashingTests, ServerDistribution);
    FRIEND_TEST(ConsistentHashingTests, ScaleUpDown);
    FRIEND_TEST(ConsistentHashingTests, ThreadSafety);
    FRIEND_TEST(ConsistentHashingTests, Performance);
    FRIEND_TEST(ConsistentHashingTests, DistributionAnalysis);
};
//---------------------------------------------------------------------------------------------------------------------
