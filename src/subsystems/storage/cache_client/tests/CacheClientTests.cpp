///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//------------------------------------------------------------------------------------------------------------
#include "CacheClientTests.h"
#include "gtest/gtest.h"
#include <chrono>  // For std::chrono::seconds
#include <cstring> // For memcmp function
#include <functional>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <thread> // For std::this_thread
#include <unordered_map>

#include "CacheClient.h"
#include "CacheClientImpl.h"
#include "CacheUtils.h"
#include "CommonLogger.h"
#include "LogUtils.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cacheutils;
//---------------------------------------------------------------------------------------------------------------------
void CacheClientTests::InitializeClient(CacheClientImpl& client, bool bEnableCalculateChecksum, bool bEnableValidateChecksum )
{
    static uint32_t nPort = atoi(getenv("CACHE_SERVER_PORT"));
    static char* authAccountName = getenv("CACHESERVER_AUTH_ACCOUNTNAME");
    static char* authAccountKey = getenv("CACHESERVER_AUTH_ACCOUNTKEY");

    const char* arrServers[] = {"127.0.0.1"};
    uint32_t nServers = 1;
    bool enableIB = false;
    bool enableCalculateChecksum = bEnableCalculateChecksum;
    bool enableValidateChecksum = bEnableValidateChecksum;
    bool enableAuth = authAccountKey != nullptr && authAccountName != nullptr;

    client.Initialize(arrServers,
                      nServers,
                      nPort,
                      enableIB,
                      enableCalculateChecksum,
                      enableValidateChecksum,
                      0,
                      enableAuth,
                      authAccountName ? authAccountName : "",
                      authAccountKey ? authAccountKey : "",
                      false,
                      true);
}
//---------------------------------------------------------------------------------------------------------------------
void CacheClientTests::SetUp()
{
    if(!getenv("CACHE_SERVER_PORT"))
    {
        GTEST_SKIP();
    }

    if(!m_cacheClientImpl.IsInitialized())
    {
        InitializeClient(m_cacheClientImpl);
    }

    if(!m_cacheClientImplCRC.IsInitialized())
    {
        InitializeClient(m_cacheClientImplCRC, true, true);
    }   
}
//---------------------------------------------------------------------------------------------------------------------
void CacheClientTests::TearDown()
{
}
//---------------------------------------------------------------------------------------------------------------------
const char* CacheClientTests::GetRandomBuffer(const uint64_t nLength)
{
    char* pBuffer = (char*)malloc(nLength);
    for(uint64_t i = 0; i < min((uint64_t)50, nLength); i++)
    {
        pBuffer[i] = rand() % 256;
    }
    return (const char*)pBuffer;
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, UploadSmallFiles)
{
    uint64_t nLength = 4 * 1024 * 1024;
    const char* pBuffer = GetRandomBuffer(nLength);

    // Without encryption
    auto status = m_cacheClientImpl.Upload(GetCorrelationId().c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    // With encryption
    status = m_cacheClientImpl.Upload(
        GetCorrelationId().c_str(), pBuffer, nLength, 0, nullptr, nullptr, GetCorrelationId().c_str());
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    // With metadata
    Metadata* pMetadata = new Metadata;
    pMetadata->arrKeyValuePairs = new KeyValuePair[5];
    pMetadata->nKeyValuePairs = 5;
    for(uint32_t i = 0; i < pMetadata->nKeyValuePairs; i++)
    {
        pMetadata->arrKeyValuePairs[i].strKey = (char*)(GetCorrelationId().c_str());
        pMetadata->arrKeyValuePairs[i].strValue = (char*)(GetCorrelationId().c_str());
    }
    status = m_cacheClientImpl.Upload(GetCorrelationId().c_str(), pBuffer, nLength, 0, nullptr, pMetadata, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    // With group ID
    status = m_cacheClientImpl.Upload(
        GetCorrelationId().c_str(), pBuffer, nLength, 0, GetCorrelationId().c_str(), nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    free((void*)pBuffer);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, UploadDuplicateFiles)
{
    uint64_t nLength = 4 * 1024 * 1024;
    const char* pBuffer = GetRandomBuffer(nLength);

    // Duplicate upload
    string filename = GetCorrelationId();
    auto status = m_cacheClientImpl.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    status = m_cacheClientImpl.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);

    // Server closes connection when duplicate download is attempted,
    // so we are never able to read the `UploadResponse::FILE_EXISTS` response.
    ASSERT_EQ(CacheClientStatus::INTERNAL_ERROR, status);

    free((void*)pBuffer);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, UploadLargeFiles)
{
    uint64_t nLength = 100 * 1024 * 1024;
    const char* pBuffer = GetRandomBuffer(nLength);

    auto status = m_cacheClientImpl.Upload(GetCorrelationId().c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::INVALID_ARGUMENT, status);

    free((void*)pBuffer);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, Initialization)
{
    const char* arrServers[] = {"127.0.0.1"};
    uint32_t nServers = 1;
    uint32_t nPort = 9065;
    bool enableIB = false;
    bool enableCalculateChecksum = false;
    bool enableValidateChecksum = false;
    const char* accountName = "my_account";
    const char* accountKey = "my_secret_key";
    {
        CacheClientImpl client;
        // First initialization
        ASSERT_EQ(CacheClientStatus::SUCCESS,
                  client.Initialize(arrServers, nServers, nPort, enableIB, enableCalculateChecksum, enableValidateChecksum, 0, true, accountName, accountKey));

        // Re-initialization
        ASSERT_EQ(CacheClientStatus::INTERNAL_ERROR,
                  client.Initialize(arrServers, nServers, nPort, enableIB, enableCalculateChecksum, enableValidateChecksum, 0, true, accountName, accountKey));
    }

    {
        CacheClientImpl client;
        // No auth credentials
        ASSERT_EQ(CacheClientStatus::SUCCESS,
                  client.Initialize(arrServers, nServers, nPort, enableIB, enableCalculateChecksum, enableValidateChecksum, 0, true, "", ""));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, DownloadNonExistent)
{
    uint64_t nLength = 1024;
    void* pBuffer = malloc(nLength);
    auto status = m_cacheClientImpl.Download(GetCorrelationId().c_str(), pBuffer, &nLength, nullptr, nullptr);
    ASSERT_EQ(status, CacheClientStatus::NOTFOUND_LOCKED);

    free(pBuffer);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, DownloadFiles)
{
    string filename = GetCorrelationId();
    uint64_t nLength = 1024;
    const void* pBuffer = GetRandomBuffer(nLength);

    auto status = m_cacheClientImpl.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    void* pBuffer2 = malloc(nLength);
    uint64_t readLength = nLength;
    status = m_cacheClientImpl.Download(filename.c_str(), pBuffer2, &readLength, nullptr, nullptr);
    ASSERT_EQ(readLength, nLength);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);
    ASSERT_EQ(memcmp(pBuffer, pBuffer2, nLength), 0);

    void* pBuffer3 = malloc(nLength * 2);
    readLength = nLength * 2;
    status = m_cacheClientImpl.Download(filename.c_str(), pBuffer3, &readLength, nullptr, nullptr);
    ASSERT_EQ(readLength, nLength);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);
    ASSERT_EQ(memcmp(pBuffer, pBuffer3, nLength), 0);

    free((void*)pBuffer);
    free((void*)pBuffer2);
    free((void*)pBuffer3);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, DownloadFilesWithKeyValuePairs)
{
    string filename = GetCorrelationId();
    uint64_t nLength = 1024;
    const void* pBuffer = GetRandomBuffer(nLength);
    void* pBuffer2 = malloc(nLength);
    uint64_t readLength = 1024;

    Metadata* pMetadata = new Metadata;
    pMetadata->nKeyValuePairs = 5;
    pMetadata->arrKeyValuePairs = new KeyValuePair[pMetadata->nKeyValuePairs];
    unordered_map<string, string> metadataMap;

    char dynStr[5] = {'a', 'b', 'c', 'd', '\0'};
    for(uint32_t i = 0; i < pMetadata->nKeyValuePairs; i++)
    {
        dynStr[0] = 'a' + i;
        pMetadata->arrKeyValuePairs[i].strKey = strdup(dynStr);
        dynStr[0] = 'A' + i;
        pMetadata->arrKeyValuePairs[i].strValue = strdup(dynStr);
        metadataMap[string(strdup(pMetadata->arrKeyValuePairs[i].strKey))] =
            string(strdup(pMetadata->arrKeyValuePairs[i].strValue));
    }

    auto status = m_cacheClientImpl.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, pMetadata, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    Metadata* pDownloadedMetadata = new Metadata;
    pDownloadedMetadata->nKeyValuePairs = 0;
    pDownloadedMetadata->arrKeyValuePairs = nullptr;
    unordered_map<string, string> downloadedMetadataMap;

    status = m_cacheClientImpl.Download(filename.c_str(), pBuffer2, &readLength, pDownloadedMetadata, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);
    ASSERT_EQ(readLength, nLength);
    ASSERT_EQ(memcmp(pBuffer, pBuffer2, nLength), 0);
    ASSERT_EQ(pDownloadedMetadata->nKeyValuePairs, pMetadata->nKeyValuePairs);
    for(uint32_t i = 0; i < pMetadata->nKeyValuePairs; i++)
    {
        downloadedMetadataMap[string(pDownloadedMetadata->arrKeyValuePairs[i].strKey)] =
            string(pDownloadedMetadata->arrKeyValuePairs[i].strValue);
    }
    ASSERT_EQ(metadataMap, downloadedMetadataMap);

    free((void*)pBuffer);
    free((void*)pBuffer2);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, DownloadFilesWithEncryption)
{
    string filename = GetCorrelationId();
    string encryptionKey = GetCorrelationId();
    uint64_t nLength = 1024;
    const void* pBuffer = GetRandomBuffer(nLength);

    auto status =
        m_cacheClientImpl.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, nullptr, encryptionKey.c_str());
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    void* pBuffer2 = malloc(nLength);
    uint64_t readLength = nLength;
    status = m_cacheClientImpl.Download(filename.c_str(), pBuffer2, &readLength, nullptr, encryptionKey.c_str());
    ASSERT_EQ(readLength, nLength);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);
    ASSERT_EQ(memcmp(pBuffer, pBuffer2, nLength), 0);

    free((void*)pBuffer);
    free((void*)pBuffer2);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, DownloadFilesWithChecksum)
{
    string filename = GetCorrelationId();
    uint64_t nLength = 1024;
    const void* pBuffer = GetRandomBuffer(nLength);

    auto status = m_cacheClientImplCRC.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    void* pBuffer2 = malloc(nLength);
    uint64_t readLength = nLength;
    status = m_cacheClientImplCRC.Download(filename.c_str(), pBuffer2, &readLength, nullptr, nullptr);
    ASSERT_EQ(readLength, nLength);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);
    ASSERT_EQ(memcmp(pBuffer, pBuffer2, nLength), 0);

    free((void*)pBuffer);
    free((void*)pBuffer2);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, DownloadFilesWithMissingChecksum)
{
    string filename = GetCorrelationId();
    uint64_t nLength = 1024;
    const void* pBuffer = GetRandomBuffer(nLength);

    auto status = m_cacheClientImpl.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    void* pBuffer2 = malloc(nLength);
    uint64_t readLength = nLength;
    status = m_cacheClientImplCRC.Download(filename.c_str(), pBuffer2, &readLength, nullptr, nullptr);
    ASSERT_EQ(readLength, nLength);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);
    ASSERT_EQ(memcmp(pBuffer, pBuffer2, nLength), 0);

    free((void*)pBuffer);
    free((void*)pBuffer2);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, DownloadPartialFileWithChecksumEnabled)
{
    string filename = GetCorrelationId();
    uint64_t nLength = 1024;
    const void* pBuffer = GetRandomBuffer(nLength);

    auto status = m_cacheClientImplCRC.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    void* pBuffer2 = malloc(nLength);
    uint64_t readLength = nLength-10;
    status = m_cacheClientImplCRC.Download(filename.c_str(), pBuffer2, &readLength, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    free((void*)pBuffer);
    free((void*)pBuffer2);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, DownloadFileWithChecksumMismatch)
{
    string filename = GetCorrelationId();
    uint64_t nLength = 1024;
    const void* pBuffer = GetRandomBuffer(nLength);
    Metadata* pMetadata = new Metadata;
    pMetadata->arrKeyValuePairs = new KeyValuePair[1];
    pMetadata->nKeyValuePairs = 1;
    pMetadata->arrKeyValuePairs[0].strKey = strdup("CHUNK_CHECKSUM");
    pMetadata->arrKeyValuePairs[0].strValue = strdup("1234567890");

    auto status = m_cacheClientImpl.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, pMetadata, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    void* pBuffer2 = malloc(nLength);
    uint64_t readLength = nLength;
    status = m_cacheClientImplCRC.Download(filename.c_str(), pBuffer2, &readLength, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::CHECKSUM_MISMATCH, status);

    free((void*)pBuffer);
    free((void*)pBuffer2);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, FileExpiry)
{
    string filename = GetCorrelationId();
    uint64_t nLength = 1024;
    const void* pBuffer = GetRandomBuffer(nLength);
    uint64_t nExpirySeconds = 5;

    auto status =
        m_cacheClientImpl.Upload(filename.c_str(), pBuffer, nLength, nExpirySeconds, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    void* pBuffer2 = malloc(nLength);
    uint64_t readLength = nLength;

    status = m_cacheClientImpl.Download(filename.c_str(), pBuffer2, &readLength, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    // Requirement: Server TTL window configured to be <= 5 seconds.
    std::this_thread::sleep_for(std::chrono::seconds(nExpirySeconds + 2));

    void* pBuffer3 = malloc(nLength);
    readLength = nLength;
    status = m_cacheClientImpl.Download(filename.c_str(), pBuffer3, &readLength, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::NOTFOUND_LOCKED, status);

    free((void*)pBuffer);
    free((void*)pBuffer2);
    free((void*)pBuffer3);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, TestDeleteGroup)
{
    string groupId = "";

    // Empty group ID
    auto status = m_cacheClientImpl.DeleteGroup(groupId.c_str());
    ASSERT_EQ(CacheClientStatus::INVALID_ARGUMENT, status);

    // Null group ID
    status = m_cacheClientImpl.DeleteGroup(nullptr);
    ASSERT_EQ(CacheClientStatus::INVALID_ARGUMENT, status);

    // Valid group ID
    groupId = GetCorrelationId();
    status = m_cacheClientImpl.DeleteGroup(groupId.c_str());
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    // Test deletion succeeds after upload
    uint64_t nLength = 1024;
    const void* pBuffer = GetRandomBuffer(nLength);
    status =
        m_cacheClientImpl.Upload(GetCorrelationId().c_str(), pBuffer, nLength, 0, groupId.c_str(), nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    status = m_cacheClientImpl.DeleteGroup(groupId.c_str());
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);
    std::this_thread::sleep_for(std::chrono::seconds(6));

    void* pBuffer2 = malloc(nLength);
    uint64_t readLength = nLength;
    status = m_cacheClientImpl.Download(GetCorrelationId().c_str(), pBuffer2, &readLength, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::NOTFOUND_LOCKED, status);

    free((void*)pBuffer);
    free((void*)pBuffer2);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, ForceReuseConnections)
{
    CacheClientImpl::nDefaultConnectionPoolSize = 1;
    CacheClientImpl client;
    InitializeClient(client);

    uint64_t nLength = 1024;
    const char* pBuffer = GetRandomBuffer(nLength);
    CacheClientStatus status;

    for(size_t i = 0; i < 32; i++)
    {
        string filename = GetCorrelationId();
        status = m_cacheClientImpl.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
        ASSERT_EQ(CacheClientStatus::SUCCESS, status);
    }

    free((void*)pBuffer);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, ParallelUploadDownload)
{
    uint64_t nLength = 4 * 1024 * 1024;
    const char* pBuffer = GetRandomBuffer(nLength);
    std::vector<std::thread> threads;
    size_t num_threads = 32;
    atomic_uint_fast64_t counter = 0;

    for(size_t i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(
            [this, &counter, pBuffer, nLength]()
            {
                string filename = to_string(counter.fetch_add(1)) + GetCorrelationId();
                LOG_INFO("Uploading file {}", filename);
                auto status =
                    this->m_cacheClientImpl.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
                ASSERT_EQ(CacheClientStatus::SUCCESS, status);

                void* pBuffer2 = malloc(nLength);
                uint64_t readLength;
                status = this->m_cacheClientImpl.Download(filename.c_str(), pBuffer2, &readLength, nullptr, nullptr);
                ASSERT_EQ(CacheClientStatus::SUCCESS, status);
                ASSERT_EQ(readLength, nLength);

                free((void*)pBuffer2);
            });
    }

    // Wait for all threads to finish
    for(auto& thread : threads)
    {
        thread.join();
    }

    free((void*)pBuffer);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTests, StaleItemExpiry)
{
    char* evictionEnabled = std::getenv("CACHESERVER_EVICTION_ENABLED");
    if (evictionEnabled == NULL || strcasecmp(evictionEnabled, "true") != 0)
    {
        LOG_INFO("Server does not have eviction enabled, skipping test...");
        GTEST_SKIP();
    }

    // Get the eviction last access threshold that the server was configured with
    // Skipping sanity checks
    char* strEvictionLastAccessThreshold = std::getenv("CACHESERVER_EVICTION_LAST_ACCESS_THRESHOLD");
    uint64_t evictionLastAccessThresholdSeconds = std::strtoull(strEvictionLastAccessThreshold, NULL, 10);

    // Upload a small file
    uint64_t nLength = 4 * 1024 * 1024;
    const char* pBuffer = GetRandomBuffer(nLength);
    std::string fileName = GetCorrelationId();

    auto status = m_cacheClientImpl.Upload(
        fileName.c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    // Download this file before eviction window elapses
    void* pDownloadBuffer = malloc(nLength);
    uint64_t readLength = nLength;
    status = m_cacheClientImpl.Download(fileName.c_str(), pDownloadBuffer, &readLength, nullptr, nullptr);
    ASSERT_EQ(readLength, nLength);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);
    ASSERT_EQ(memcmp(pBuffer, pDownloadBuffer, nLength), 0);

    memset(pDownloadBuffer, 0, nLength);

    // Wait for the eviction last access threshold to pass
    std::this_thread::sleep_for(std::chrono::seconds(evictionLastAccessThresholdSeconds + 1));
    readLength = nLength;
    status = m_cacheClientImpl.Download(fileName.c_str(), pDownloadBuffer, &readLength, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::NOTFOUND_LOCKED, status);

    free((void*)pBuffer);
    free(pDownloadBuffer);
}
//---------------------------------------------------------------------------------------------------------------------