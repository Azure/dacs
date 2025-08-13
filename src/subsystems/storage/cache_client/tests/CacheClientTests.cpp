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
#include <iomanip>
#include <sstream>
#include <ctime>

#include "CacheClient.h"
#include "CacheClientImpl.h"
#include "CacheUtils.h"
#include "CommonLogger.h"
#include "LogUtils.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cacheutils;
//---------------------------------------------------------------------------------------------------------------------
/**
 * ### Key Features:
 * 1. **Parameterized Tests**:
 *    - The parameter `bIsDiscoveryEnabled` is passed to each test case, allowing the tests to run 
 *      with both `true` and `false` values.
 *    - This ensures that the behavior of CacheClientImpl is validated in both discovery-enabled 
 *      and discovery-disabled scenarios.
 *
 * 2. **Test Initialization**:
 *    - The `SetUp()` method initializes the CacheClientImpl instance based on the value of 
 *      `bIsDiscoveryEnabled`.
 *
 * 3. **Test Instantiation**:
 *    - The `INSTANTIATE_TEST_SUITE_P` macro is used to instantiate the test suite with two values 
 *      for `bIsDiscoveryEnabled`: `true` and `false`.
 *    - This ensures that all test cases are executed twice, once with discovery enabled and once 
 *      with discovery disabled.
 */
//---------------------------------------------------------------------------------------------------------------------
void CacheClientTests::InitializeClient(CacheClientImpl& client, bool bIsDiscoveryEnabled , bool bEnableCalculateChecksum, bool bEnableValidateChecksum )
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

    /**
     * The `nCacheServerDiscoveryIntervalInSeconds` is set to 2 seconds because
     * the destructor of `ConnectionManager` waits for the background thread
     * to finish its sleep cycle. Setting a larger value would increase the time
     * required for the test to complete.
     * Since each test creates a new instance of `CacheClientImpl`, this delay
     * impacts the execution time of every test in the suite.
     */
    client.Initialize(bIsDiscoveryEnabled ? nullptr : arrServers, /** arrServers*/
                      nServers, /** nServers*/
                      nPort, /** nPort*/
                      enableIB, /** enableIB*/
                      enableCalculateChecksum, /** enableCalculateChecksum*/
                      enableValidateChecksum, /** enableValidateChecksum*/
                      0, /** nConnectionPoolSize*/
                      enableAuth, /** enableAuth*/
                      authAccountName ? authAccountName : "", /** strAccountName*/
                      authAccountKey ? authAccountKey : "", /** strAccountKey*/
                      false, /** bUseLocalServer*/
                      true, /** bReuseConnections*/
                      bIsDiscoveryEnabled, /** bIsCacheServerDiscoveryEnabled*/
                      2, /** nCacheServerDiscoveryIntervalInSeconds*/
                      arrServers[0] /** nCacheServerDiscoveryEndpoint*/
                    );
}
//---------------------------------------------------------------------------------------------------------------------
void CacheClientTests::SetUp()
{
    bool bIsDiscoveryEnabled = GetParam(); // Get the parameter value
    LOG_INFO("CacheClientTests::SetUp() bIsDiscoveryEnabled={}", bIsDiscoveryEnabled);

    // Setting up the endpoint format for local server (testing purpose only)
    if (bIsDiscoveryEnabled) {
        SetCacheServerCount(1);

        ofstream configFile("/serverConfig/cache_server_endpoint_format");
        configFile << "127.0.{}.1"; // with one cacheservers, will be 127.0.0.1
        configFile.close();
    }

    if(!getenv("CACHE_SERVER_PORT"))
    {
        GTEST_SKIP();
    }

    if(!m_cacheClientImpl.IsInitialized())
    {
        InitializeClient(m_cacheClientImpl, bIsDiscoveryEnabled);
    }

    if(!m_cacheClientImplCRC.IsInitialized())
    {
        InitializeClient(m_cacheClientImplCRC, bIsDiscoveryEnabled, true, true);
    }   
}
//---------------------------------------------------------------------------------------------------------------------
void CacheClientTests::TearDown()
{
}
//---------------------------------------------------------------------------------------------------------------------
const char* CacheClientTests::GetRandomBuffer(const uint64_t nLength) const
{
    char* pBuffer = (char*)malloc(nLength);
    for(uint64_t i = 0; i < min((uint64_t)50, nLength); i++)
    {
        pBuffer[i] = rand() % 256;
    }
    return (const char*)pBuffer;
}
//---------------------------------------------------------------------------------------------------------------------
/**
 * Set the cache server count in the configuration file.
 * This function creates a directory named "/serverConfig" if it doesn't exist,
 * and writes the specified count to a file named "cache_server_count" within that directory.
 */
void CacheClientTests::SetCacheServerCount(int count) const {
    string path = "/serverConfig";
    if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST)
    {
        LOG_ERROR("Failed to create directory: {}. Error: {}", path, strerror(errno));
        assert(false && "Directory creation failed");
    }
    ofstream configFile("/serverConfig/cache_server_count");
    configFile << count;
    configFile.close();
}
//---------------------------------------------------------------------------------------------------------------------
bool CacheClientTests::WaitForServerCount(int expectedCount, int timeoutSeconds) const {
    for (int i = 0; i < timeoutSeconds; i++) {
        auto servers = m_cacheClientImpl.m_spConnectionManager->GetAllCacheServerHostnames();
        if (servers.size() == expectedCount) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
void CacheClientTests::AssertServersContain(const std::vector<std::string>& servers, const std::string& expectedServer) const {
    auto it = std::find(servers.begin(), servers.end(), expectedServer);
    ASSERT_NE(it, servers.end()) << "Expected server " << expectedServer << " not found in server list";
}
//---------------------------------------------------------------------------------------------------------------------
TEST_P(CacheClientTests, UploadSmallFiles)
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
TEST_P(CacheClientTests, UploadDuplicateFiles)
{
    uint64_t nLength = 4 * 1024 * 1024;
    const char* pBuffer = GetRandomBuffer(nLength);

    // Duplicate upload
    string filename = GetCorrelationId();
    auto status = m_cacheClientImpl.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    status = m_cacheClientImpl.Upload(filename.c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);

    ASSERT_EQ(CacheClientStatus::KEY_EXISTS, status);

    free((void*)pBuffer);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_P(CacheClientTests, UploadLargeFiles)
{
    uint64_t nLength = 256 * 1024 * 1024;
    const char* pBuffer = GetRandomBuffer(nLength);

    auto status = m_cacheClientImpl.Upload(GetCorrelationId().c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::INVALID_ARGUMENT, status);

    free((void*)pBuffer);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_P(CacheClientTests, Initialization)
{
    const char* arrServers[] = {"127.0.0.1"};
    uint32_t nServers = 1;
    uint32_t nPort = 9065;
    bool enableIB = false;
    bool enableCalculateChecksum = false;
    bool enableValidateChecksum = false;
    const char* accountName = "my_account";
    const char* accountKey = "my_secret_key";

    // This test checks the behavior of CacheClientImpl initialization
    // with various parameters, including discovery enabled and disabled.
    // Repeated initialization should return INTERNAL_ERROR.
    {
        CacheClientImpl client;
        // First initialization
        ASSERT_EQ(CacheClientStatus::SUCCESS,
            client.Initialize(arrServers /**arrServers*/,
                                nServers /**nServers*/,
                                nPort /**nPort*/,
                                enableIB /**enableIB*/,
                                enableCalculateChecksum /**enableCalculateChecksum*/,
                                enableValidateChecksum /**enableValidateChecksum*/,
                                0 /**nConnectionPoolSize*/,
                                true /**enableAuth*/,
                                accountName /**accountName*/,
                                accountKey /**accountKey*/,
                                false /**useLocalServer*/,
                                true /**bReuseConnections*/,
                                false /**bIsCacheServerDiscoveryEnabled*/,
                                5 /**nCacheServerDiscoveryIntervalInSeconds*/,
                                arrServers[0] /**nCacheServerDiscoveryEndpoint*/));

        // Re-initialization
        ASSERT_EQ(CacheClientStatus::INTERNAL_ERROR,
            client.Initialize(arrServers /**arrServers*/,
                                nServers /**nServers*/,
                                nPort /**nPort*/,
                                enableIB /**enableIB*/,
                                enableCalculateChecksum /**enableCalculateChecksum*/,
                                enableValidateChecksum /**enableValidateChecksum*/,
                                0 /**nConnectionPoolSize*/,
                                true /**enableAuth*/,
                                accountName /**accountName*/,
                                accountKey /**accountKey*/,
                                false /**useLocalServer*/,
                                true /**bReuseConnections*/,
                                false /**bIsCacheServerDiscoveryEnabled*/,
                                5 /**nCacheServerDiscoveryIntervalInSeconds*/,
                                arrServers[0] /**nCacheServerDiscoveryEndpoint*/));

        // Re-initialization with discovery enabled
        ASSERT_EQ(CacheClientStatus::INTERNAL_ERROR,
            client.Initialize(arrServers /**arrServers*/,
                                nServers /**nServers*/,
                                nPort /**nPort*/,
                                enableIB /**enableIB*/,
                                enableCalculateChecksum /**enableCalculateChecksum*/,
                                enableValidateChecksum /**enableValidateChecksum*/,
                                0 /**nConnectionPoolSize*/,
                                true /**enableAuth*/,
                                accountName /**accountName*/,
                                accountKey /**accountKey*/,
                                false /**useLocalServer*/,
                                true /**bReuseConnections*/,
                                true /**bIsCacheServerDiscoveryEnabled*/,
                                2 /**nCacheServerDiscoveryIntervalInSeconds*/,
                                arrServers[0] /**nCacheServerDiscoveryEndpoint*/));
    }

    // This test checks the behavior of CacheClientImpl initialization
    // with an empty server list and discovery enabled.
    // Discovery endpoint is empty, so it should fail.
    // It should return INTERNAL_ERROR.
    {
        CacheClientImpl client;

        ASSERT_EQ(CacheClientStatus::INVALID_ARGUMENT,
            client.Initialize(nullptr, /**arrServers*/
                             0, /**nServers*/
                             nPort, /**nPort*/
                             false, /**enableIB*/
                             false, /**enableCalculateChecksum*/
                             false, /**enableValidateChecksum*/
                             0, /**nConnectionPoolSize*/
                             false, /**enableAuth*/
                             "", /**accountName*/
                             "", /**accountKey*/
                             false, /**useLocalServer*/
                             true, /**bReuseConnections*/
                             true, /**bIsCacheServerDiscoveryEnabled*/
                             2, /**nCacheServerDiscoveryIntervalInSeconds*/
                             "" /**nCacheServerDiscoveryEndpoint - Empty endpoint*/));
    }

    // This test checks the behavior of CacheClientImpl initialization
    // with no auth credentials.
    {
        CacheClientImpl client;
        // No auth credentials
        ASSERT_EQ(CacheClientStatus::SUCCESS,
            client.Initialize(arrServers /**arrServers*/,
                nServers /**nServers*/,
                nPort /**nPort*/,
                enableIB /**enableIB*/,
                enableCalculateChecksum /**enableCalculateChecksum*/,
                enableValidateChecksum /**enableValidateChecksum*/,
                0 /**nConnectionPoolSize*/,
                true /**enableAuth*/,
                "" /**accountName*/,
                "" /**accountKey*/,
                false /**useLocalServer*/,
                true /**bReuseConnections*/,
                false /**bIsCacheServerDiscoveryEnabled*/,
                5 /**nCacheServerDiscoveryIntervalInSeconds*/,
                arrServers[0] /**nCacheServerDiscoveryEndpoint*/));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_P(CacheClientTests, DownloadNonExistent)
{
    uint64_t nLength = 1024;
    void* pBuffer = malloc(nLength);
    auto status = m_cacheClientImpl.Download(GetCorrelationId().c_str(), pBuffer, &nLength, nullptr, nullptr);
    ASSERT_EQ(status, CacheClientStatus::NOTFOUND_LOCKED);

    free(pBuffer);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_P(CacheClientTests, DownloadFiles)
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
TEST_P(CacheClientTests, DownloadFilesWithKeyValuePairs)
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
TEST_P(CacheClientTests, DownloadFilesWithEncryption)
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
TEST_P(CacheClientTests, DownloadFilesWithChecksum)
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
TEST_P(CacheClientTests, DownloadFilesWithMissingChecksum)
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
TEST_P(CacheClientTests, DownloadPartialFileWithChecksumEnabled)
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
TEST_P(CacheClientTests, DownloadFileWithChecksumMismatch)
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
TEST_P(CacheClientTests, FileExpiry)
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
TEST_P(CacheClientTests, TestDeleteGroup)
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
TEST_P(CacheClientTests, TestGetLastAccessTimeDistribution)
{
    uint64_t distributionBucketSizeInHours;
    LastAccessTimeDistribution* pLastAccessTimeDistribution = new LastAccessTimeDistribution;
    pLastAccessTimeDistribution->arrKeyValuePairs = nullptr;
    pLastAccessTimeDistribution->nKeyValuePairs = 0;

    // distribution window <= 0
    distributionBucketSizeInHours = 0;
    auto status = m_cacheClientImpl.GetLastAccessTimeDistribution(distributionBucketSizeInHours, pLastAccessTimeDistribution, nullptr);
    ASSERT_EQ(CacheClientStatus::INVALID_ARGUMENT, status);

    uint64_t nLength = 1024;
    const void* pBuffer = GetRandomBuffer(nLength);
    status =
        m_cacheClientImpl.Upload(GetCorrelationId().c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);
    // Valid correlation ID
    distributionBucketSizeInHours = 1;
    status = m_cacheClientImpl.GetLastAccessTimeDistribution(distributionBucketSizeInHours, pLastAccessTimeDistribution, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);
    ASSERT_EQ(pLastAccessTimeDistribution->nKeyValuePairs, 1);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_P(CacheClientTests, TestDeleteByLastAccessTime)
{
    string lastAccessTimestamp = "";
    // Empty Timestamp
    auto status = m_cacheClientImpl.DeleteByLastAccessTime(lastAccessTimestamp.c_str());
    ASSERT_EQ(CacheClientStatus::INVALID_ARGUMENT, status);

    // Null Timestamp
    status = m_cacheClientImpl.DeleteByLastAccessTime(nullptr);
    ASSERT_EQ(CacheClientStatus::INVALID_ARGUMENT, status);

    //Invalid Timestamp
    lastAccessTimestamp = "4:00:00";
    status = m_cacheClientImpl.DeleteByLastAccessTime(lastAccessTimestamp.c_str());
    ASSERT_EQ(CacheClientStatus::INVALID_ARGUMENT, status);

    //Invalid Timestamp
    lastAccessTimestamp = "2024-13-02T20:00:00.000Z";
    status = m_cacheClientImpl.DeleteByLastAccessTime(lastAccessTimestamp.c_str());
    ASSERT_EQ(CacheClientStatus::INVALID_ARGUMENT, status);

    //Invalid Timestamp
    lastAccessTimestamp = "2024-12-02T20:00:00.000+05:30";
    status = m_cacheClientImpl.DeleteByLastAccessTime(lastAccessTimestamp.c_str());
    ASSERT_EQ(CacheClientStatus::INVALID_ARGUMENT, status);

    // Valid Timestamp
    uint64_t nLength = 1024;
    const void* pBuffer = GetRandomBuffer(nLength);
    status = m_cacheClientImpl.Upload(GetCorrelationId().c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    time_t timestamp = time(nullptr);
    std::ostringstream oss;
    std::tm* tm = gmtime(&timestamp); // Use gmtime to get UTC time    
    oss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S") << ".456Z";
    lastAccessTimestamp = oss.str();
    status = m_cacheClientImpl.DeleteByLastAccessTime(lastAccessTimestamp.c_str());
    ASSERT_EQ(CacheClientStatus::SUCCESS, status);

    this_thread::sleep_for(chrono::seconds(3));

    void* pBuffer2 = malloc(nLength);
    uint64_t readLength = nLength;
    status = m_cacheClientImpl.Download(GetCorrelationId().c_str(), pBuffer2, &readLength, nullptr, nullptr);
    ASSERT_EQ(CacheClientStatus::NOTFOUND_LOCKED, status);

    free((void*)pBuffer);
    free((void*)pBuffer2);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_P(CacheClientTests, ForceReuseConnections)
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
TEST_P(CacheClientTests, ParallelUploadDownload)
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
TEST_P(CacheClientTests, StaleItemExpiry)
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
TEST_P(CacheClientTests, RefreshCacheServersWithDiscoveryEndpoint) {
    if (!IsDiscoveryTest()) {
        GTEST_SKIP();
    }
    
    // Test server count changes
    SetCacheServerCount(1);
    ASSERT_TRUE(WaitForServerCount(1, DEFAULT_DISCOVERY_TIMEOUT));
    
    SetCacheServerCount(2);
    ASSERT_TRUE(WaitForServerCount(2, DEFAULT_DISCOVERY_TIMEOUT));
    
    // Reset for other tests - use extended timeout for reset
    SetCacheServerCount(1);
    ASSERT_TRUE(WaitForServerCount(1, EXTENDED_DISCOVERY_TIMEOUT));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_P(CacheClientTests, DiscoveryEndpointConfigurationValidation) {
    // Test invalid configurations
    {
        CacheClientImpl client;
        uint32_t nPort = atoi(getenv("CACHE_SERVER_PORT"));
        
        ASSERT_EQ(CacheClientStatus::INVALID_ARGUMENT,
            client.Initialize(nullptr, 0, nPort, false, false, false, 0, false, "", "", 
                             false, true, true, 2, ""));  // Empty endpoint
    }
    
    // Test valid configuration
    {
        CacheClientImpl client;
        InitializeClient(client, true);
        ASSERT_TRUE(client.IsInitialized());
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_P(CacheClientTests, ResilientInitializationWithNetworkFailures) {
    if (!IsDiscoveryTest()) {
        GTEST_SKIP();
    }
    
    CacheClientImpl client;
    uint32_t nPort = atoi(getenv("CACHE_SERVER_PORT"));
    
    // Should succeed with unreachable endpoint (graceful degradation)
    ASSERT_EQ(CacheClientStatus::SUCCESS,
        client.Initialize(nullptr, 0, nPort, false, false, false, 0, false, "", "", 
                         false, true, true, 2, UNREACHABLE_ENDPOINT));
    
    // Verify graceful degradation
    auto servers = client.m_spConnectionManager->GetAllCacheServerHostnames();
    ASSERT_EQ(servers.size(), 0);
    
    // Operations should fail gracefully
    uint64_t nLength = 1024;
    const char* pBuffer = GetRandomBuffer(nLength);
    auto status = client.Upload(GetCorrelationId().c_str(), pBuffer, nLength, 0, nullptr, nullptr, nullptr);
    ASSERT_NE(CacheClientStatus::SUCCESS, status);
    
    free((void*)pBuffer);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_P(CacheClientTests, DiscoveryResilience) {
    if (!IsDiscoveryTest()) GTEST_SKIP();
    
    // Setup initial state
    SetCacheServerCount(1);
    ASSERT_TRUE(WaitForServerCount(1, DEFAULT_DISCOVERY_TIMEOUT));
    
    // Test resilience to invalid API responses
    SetCacheServerCount(0);
    std::this_thread::sleep_for(std::chrono::seconds(DEFAULT_DISCOVERY_TIMEOUT));
    auto servers = m_cacheClientImpl.m_spConnectionManager->GetAllCacheServerHostnames();
    ASSERT_EQ(servers.size(), 1);  // Should remain unchanged
    
    // Test recovery
    SetCacheServerCount(2);
    ASSERT_TRUE(WaitForServerCount(2, EXTENDED_DISCOVERY_TIMEOUT));
    
    // Reset
    SetCacheServerCount(1);
    ASSERT_TRUE(WaitForServerCount(1, DEFAULT_DISCOVERY_TIMEOUT));
}
//---------------------------------------------------------------------------------------------------------------------
// Instantiate the test suite with `bIsDiscoveryEnabled` values of `true` and `false`
INSTANTIATE_TEST_SUITE_P(
    DiscoveryEnabledTests, // Prefix for the test suite
    CacheClientTests,      // Test suite name
    ::testing::Values(true, false) // Values for `bIsDiscoveryEnabled` as true and false
);
//---------------------------------------------------------------------------------------------------------------------