//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheClientTest.h"
#include "LocalFSCacheClient.h"
#include "RemoteCacheClient.h"
#include "LocalFSCacheClient.h"
#include "CacheUtils.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptUtils.h"
#include <algorithm>
#include <cctype>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <uuid/uuid.h>
#include <openssl/evp.h>
#include <time.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace cacheutils;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
CacheClientTest::CacheClientTest()
    : TestBase()
{
    uuid_t out;
    uuid_generate(out);
    char testId[37];
    uuid_unparse(out, testId);
    m_strTestId = testId;
    if (StorageInterceptConfig::GetInstance().GetCacheEnable("") && 
        !StorageInterceptConfig::GetInstance().GetCacheUseLocalFS())
    {
        m_cacheClient = make_shared<RemoteCacheClient>(16 * 1024 * 1024);
    }

    const char* envRunExtensiveTests = std::getenv("STORAGE_INTERCEPT_RUN_EXTENSIVE_TESTS");
    if (envRunExtensiveTests == nullptr)
    {
        m_bRunExtensiveTests = false;
    }
    else
    {
        std::string strRunExtensiveTests(envRunExtensiveTests);
        std::transform(strRunExtensiveTests.begin(),
            strRunExtensiveTests.end(),
            strRunExtensiveTests.begin(),
            [](unsigned char c){ return std::tolower(c); });

        if (strRunExtensiveTests.compare("true") == 0)
        {
            m_bRunExtensiveTests = true;
        }
        else
        {
            m_bRunExtensiveTests = false;
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
void CacheClientTest::SetUp()
{
}
//---------------------------------------------------------------------------------------------------------------------
atomic_int CacheClientTest::m_fileCount = 0;
//---------------------------------------------------------------------------------------------------------------------
string CacheClientTest::GetRandFilename()
{
    return "test-" + m_strTestId + "-" + std::to_string(m_fileCount++);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTest, SimpleUploadDownloadTest)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    string version = StorageInterceptUtils::GenerateUUID();
    uint64_t dataRead = 0;
    CacheStatus cacheStatus;
    bool bRetVal;

    // Test 4MB file
    std::vector<char> outBuffer(1024*1024*4);
    string strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND);

    // repeat not found
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND);

    auto dataBuffer = GetRandDataBuffer(1024*1024*4);
    bRetVal = m_cacheClient->Upload("", strFileName, version, 0, dataBuffer, false /*isWrite*/);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    // repeat download
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, 0, outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTest, DifferentFileSizesUploadDownloadTest)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    string version = StorageInterceptUtils::GenerateUUID();
    uint64_t dataRead = 0;
    CacheStatus cacheStatus;
    bool bRetVal;

    // 1 byte file test
    std::vector<char> outBuffer(1);
    string strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND);

    auto dataBuffer = GetRandDataBuffer(1);
    bRetVal = m_cacheClient->Upload("", strFileName, version, 0, dataBuffer, false /*isWrite*/);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    // 4MB - 1 byte file test
    outBuffer.resize(1024*1024*4 - 1);
    strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND);

    dataBuffer = GetRandDataBuffer(1024*1024*4 - 1);
    bRetVal = m_cacheClient->Upload("", strFileName, version, 0, dataBuffer, false /*isWrite*/);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4 - 1);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    // 4MB + 1 byte file test
    outBuffer.resize(1024*1024*4 + 1);
    strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND);

    dataBuffer = GetRandDataBuffer(1024*1024*4 + 1);
    bRetVal = m_cacheClient->Upload("", strFileName, version, 0, dataBuffer, false /*isWrite*/);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4 + 1);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    // 10MB file test
    // We currently don't support > 4MB upload/downloads when using Infiniband
    if (!StorageInterceptConfig::GetInstance().GetCacheIBEnabled())
    {
        outBuffer.resize(1024*1024*10);
        strFileName = GetRandFilename();
        cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
        ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

        cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
        ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND);

        dataBuffer = GetRandDataBuffer(1024*1024*10);
        bRetVal = m_cacheClient->Upload("", strFileName, version, 0, dataBuffer, false /*isWrite*/);
        ASSERT_EQ(bRetVal, true);

        // wait for async upload to complete 
        // TODO: make this deterministic
        sleep(1);

        cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
        ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
        ASSERT_EQ(dataRead, 1024*1024*10);
        AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTest, ZeroSizeUploadDownloadTest)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    string version = StorageInterceptUtils::GenerateUUID();
    uint64_t dataRead = 0;
    CacheStatus cacheStatus;
    bool bRetVal;
    std::vector<char> outBuffer(100);
    
    string strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND);

    auto dataBuffer = GetRandDataBuffer(0);
    bRetVal = m_cacheClient->Upload("", strFileName, version, 0, dataBuffer, false /*isWrite*/);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTest, PartialDownloadTest)
{
    // Partial downloads are not supported with encryption enabled.
    if (this->m_cacheClient == nullptr || StorageInterceptConfig::GetInstance().GetCacheEncryptionEnabled(""))
    {
        GTEST_SKIP();
    }

    string version = StorageInterceptUtils::GenerateUUID();
    uint64_t dataRead = 0;
    CacheStatus cacheStatus;
    bool bRetVal;
    std::vector<char> outBuffer(1024*1024*4);

    string strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND);

    auto dataBuffer = GetRandDataBuffer(1024*1024*4);
    bRetVal = m_cacheClient->Upload("", strFileName, version, 0, dataBuffer, false /*isWrite*/);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, 5, outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 5);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 5, 25, outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 25);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get() + 5, dataRead);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0,
        1024*1024*4 - 1,
        outBuffer.size(),
        outBuffer.data(),
        dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get() + 1024*1024*4 - 1, dataRead);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 1024*1024*4, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 0);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 999, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4 - 999);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get() + 999, dataRead);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTest, ParallelUploadDownloadTest)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    string version = StorageInterceptUtils::GenerateUUID();
    uint64_t dataRead = 0;
    CacheStatus cacheStatus;
    bool bRetVal;

    // Create many files of different file sizes
    int numFiles = 200;
    string strFileName = GetRandFilename();
    vector<std::shared_ptr<FileData>> dataBuffer(numFiles);
    vector<vector<char>> outBuffer(numFiles);
    vector<string> fileNames(numFiles);
    for (int i = 0; i < numFiles; i++)
    {
        int randLength = (4*1024*1024 - 10) + (rand() % 21);
        dataBuffer[i] = GetRandDataBuffer(randLength);
        outBuffer[i].resize(randLength);
        fileNames[i] = strFileName + "-" + to_string(i);
    }

    // Get lock for all files in parallel
    vector<unique_ptr<thread>> testThreads;
    for (int i = 0; i < numFiles; ++i)
    {
        auto t = make_unique<thread>([i, &outBuffer, &fileNames, &version, this]() {
            uint64_t dataRead = 0;
            auto cacheStatus =
                this->m_cacheClient->DownloadSync("", fileNames[i], version, 0, 0, outBuffer[i].size(), outBuffer[i].data(), dataRead);
            ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);
        });
        testThreads.push_back(move(t));
    }
    for (auto &t : testThreads)
    {
        t->join();
    }
    testThreads.clear();

    // upload all files in parallel
    for (int i = 0; i < numFiles; ++i)
    {
        auto t = make_unique<thread>([i, &dataBuffer, &fileNames, &version, this]() {
            bool bRetVal = this->m_cacheClient->Upload("", fileNames[i], version, 0, dataBuffer[i], false /*isWrite*/);
            ASSERT_EQ(bRetVal, true);
        });
        testThreads.push_back(move(t));
    }
    for (auto &t : testThreads)
    {
        t->join();
    }
    testThreads.clear();

    // wait for async uploads to complete
    // TODO: make this deterministic
    sleep(5);

    // download all files in parallel
    for (int i = 0; i < numFiles; ++i)
    {
        auto t = make_unique<thread>([i, &dataBuffer, &outBuffer, &fileNames, &version, this]() {
            uint64_t dataRead = 0;
            auto cacheStatus =
                this->m_cacheClient->DownloadSync("", fileNames[i], version, 0, 0, outBuffer[i].size(), outBuffer[i].data(), dataRead);
            ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
            ASSERT_EQ(dataRead, dataBuffer[i]->BufferSize);
            AssertEqArray<char>(outBuffer[i].data(), dataBuffer[i]->Buffer.get(), dataRead);
        });
        testThreads.push_back(move(t));
    }
    for (auto &t : testThreads)
    {
        t->join();
    }
    testThreads.clear();
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTest, TestUploadLockExpiry)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    string version = StorageInterceptUtils::GenerateUUID();

    // Only one (first) client out of many should get LOCKED status for a file
    vector<unique_ptr<thread>> testThreads;
    string strFileName = GetRandFilename();
    std::atomic_uint_fast32_t numLocks = 0;
    for (int i = 0; i < 20; ++i)
    {
        auto t = make_unique<thread>([&strFileName, &numLocks, &version, this]() {
            std::vector<char> outBuffer(1024*1024*4);
            uint64_t dataRead = 0;
            auto cacheStatus =
                this->m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
            ASSERT_TRUE(cacheStatus == CacheStatus::NOTFOUND_LOCKED || cacheStatus == CacheStatus::NOTFOUND);
            if (cacheStatus == CacheStatus::NOTFOUND_LOCKED)
            {
                numLocks++;
            }
        });
        testThreads.push_back(move(t));
    }
    for (auto &t : testThreads)
    {
        t->join();
    }
    testThreads.clear();

    ASSERT_EQ(numLocks, 1);
    numLocks = 0;

    // The lock status should get reset after a while and we should be able to get the lock status again
    time_t startTime = time(NULL);

    // Loop for a limited time to prevent hang on test failure
    while (difftime(time(NULL), startTime) < 120)
    {
        std::vector<char> outBuffer(1024*1024*4);
        uint64_t dataRead = 0;
        auto cacheStatus =
            this->m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
        ASSERT_TRUE(cacheStatus == CacheStatus::NOTFOUND_LOCKED || cacheStatus == CacheStatus::NOTFOUND);
        if (cacheStatus == CacheStatus::NOTFOUND_LOCKED)
        {
            numLocks++;
            break;
        }

        // sleep for a while to avoid cpu usage
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }

    ASSERT_EQ(numLocks, 1);

    // We should not get LOCK for a while
    std::vector<char> outBuffer(1024*1024*4);
    uint64_t dataRead = 0;
    auto cacheStatus =
        this->m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_TRUE(cacheStatus == CacheStatus::NOTFOUND);

    cacheStatus =
        this->m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_TRUE(cacheStatus == CacheStatus::NOTFOUND);
}
//---------------------------------------------------------------------------------------------------------------------
// This test is meant to emulate the StreamingClient::ReadChunkImpl loop
TEST_F(CacheClientTest, TestAvoidRemoteDownloadLoopWithUploadSuccessful)
{
    if (this->m_cacheClient == nullptr || !this->m_bRunExtensiveTests)
    {
        GTEST_SKIP();
    }

    string version = StorageInterceptUtils::GenerateUUID();

    // Test 4MB file
    std::vector<char> outBuffer(1024*1024*4);
    string strFileName = GetRandFilename();

    unique_ptr<thread> t1 = make_unique<thread>([&outBuffer, &strFileName, &version, this]() {
        uint64_t dataRead = 0;
        auto cacheStatus =
            this->m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
        ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

        this_thread::sleep_for(chrono::seconds(30));
        auto dataBuffer = GetRandDataBuffer(1024*1024*4);
        bool bRetVal =
            m_cacheClient->Upload("", strFileName, version, 0, dataBuffer, false /*isWrite*/);
        ASSERT_EQ(bRetVal, true);
    });

    unique_ptr<thread> t2 = make_unique<thread>([&outBuffer, &strFileName, &version, this]() {
        this_thread::sleep_for(chrono::seconds(2));
        uint64_t dataRead = 0;
        CacheStatus cacheStatus = CacheStatus::NOTFOUND;
        int nMaxAttempts = 50;
        int nIteration = 0;
        do
        {
            if(nIteration > 0)
            {
                this_thread::sleep_for(chrono::seconds(2));
            }

            cacheStatus = this->m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);

            nIteration++;
        }
        while(cacheStatus == CacheStatus::NOTFOUND && nIteration < nMaxAttempts);
        ASSERT_EQ(CacheStatus::SUCCESS, cacheStatus);
        ASSERT_FALSE(nIteration >= nMaxAttempts);
        ASSERT_EQ(dataRead, 1024*1024*4);
    });

    t1->join();
    t2->join();
}
//---------------------------------------------------------------------------------------------------------------------
// This test is meant to emulate the StreamingClient::ReadChunkImpl loop
TEST_F(CacheClientTest, TestAvoidRemoteDownloadLoopWithLeaseExpiry)
{
    if (this->m_cacheClient == nullptr || !this->m_bRunExtensiveTests)
    {
        GTEST_SKIP();
    }

    string version = StorageInterceptUtils::GenerateUUID();

    // Test 4MB file
    std::vector<char> outBuffer(1024*1024*4);
    string strFileName = GetRandFilename();

    unique_ptr<thread> t1 = make_unique<thread>([&outBuffer, &strFileName, &version, this]() {
        uint64_t dataRead = 0;
        auto cacheStatus =
            this->m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
        ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);
    });

    unique_ptr<thread> t2 = make_unique<thread>([&outBuffer, &strFileName, &version, this]() {
        this_thread::sleep_for(chrono::seconds(2));
        uint64_t dataRead = 0;
        CacheStatus cacheStatus = CacheStatus::NOTFOUND;
        int nMaxAttempts = 50;
        int nIteration = 0;
        do
        {
            if(nIteration > 0)
            {
                this_thread::sleep_for(chrono::seconds(2));
            }

            cacheStatus = this->m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);

            nIteration++;
        }
        while(cacheStatus == CacheStatus::NOTFOUND && nIteration < nMaxAttempts);
        ASSERT_EQ(CacheStatus::NOTFOUND_LOCKED, cacheStatus);
        ASSERT_FALSE(nIteration >= nMaxAttempts);
    });

    t1->join();
    t2->join();
}
//---------------------------------------------------------------------------------------------------------------------
// This test is meant to emulate the StreamingClient::ReadChunkImpl loop
TEST_F(CacheClientTest, TestAvoidRemoteDownloadLoopWithoutLeaseExpiry)
{
    if (this->m_cacheClient == nullptr || !this->m_bRunExtensiveTests)
    {
        GTEST_SKIP();
    }

    string version = StorageInterceptUtils::GenerateUUID();

    // Test 4MB file
    std::vector<char> outBuffer(1024*1024*4);
    string strFileName = GetRandFilename();

    unique_ptr<thread> t1 = make_unique<thread>([&outBuffer, &strFileName, &version, this]() {
        uint64_t dataRead = 0;
        auto cacheStatus =
            this->m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
        ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);
    });

    unique_ptr<thread> t2 = make_unique<thread>([&outBuffer, &strFileName, &version, this]() {
        this_thread::sleep_for(chrono::seconds(2));
        uint64_t dataRead = 0;
        CacheStatus cacheStatus = CacheStatus::NOTFOUND;
        int nMaxAttempts = 10;
        int nIteration = 0;
        do
        {
            if(nIteration > 0)
            {
                this_thread::sleep_for(chrono::seconds(2));
            }

            cacheStatus = this->m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);

            nIteration++;
        }
        while(cacheStatus == CacheStatus::NOTFOUND && nIteration < nMaxAttempts);
        ASSERT_EQ(CacheStatus::NOTFOUND, cacheStatus);
        ASSERT_TRUE(nIteration >= nMaxAttempts);
    });

    t1->join();
    t2->join();
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTest, VersionedUploadDownloadTest)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    uint64_t dataRead = 0;
    CacheStatus cacheStatus;
    bool bRetVal;

    std::vector<char> outBuffer(1024*1024*4);
    string strFileName = GetRandFilename();
    string version1 = StorageInterceptUtils::GenerateUUID();
    string version2 = StorageInterceptUtils::GenerateUUID();

    // Upload same file with version1
    auto dataBufferV1 = GetRandDataBuffer(1024*1024*4);
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version1, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    bRetVal = m_cacheClient->Upload("", strFileName, version1, 0, dataBufferV1, false /*isWrite*/);
    ASSERT_EQ(bRetVal, true);
    sleep(1);

    // Upload same file with version2 (different data)
    auto dataBufferV2 = GetRandDataBuffer(1024*1024*4);
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version2, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    bRetVal = m_cacheClient->Upload("", strFileName, version2, 0, dataBufferV2, false /*isWrite*/);
    ASSERT_EQ(bRetVal, true);
    sleep(1);

    // Download version1 — should get version1 data
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version1, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4);
    AssertEqArray<char>(outBuffer.data(), dataBufferV1->Buffer.get(), dataRead);

    // Download version2 — should get version2 data
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version2, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4);
    AssertEqArray<char>(outBuffer.data(), dataBufferV2->Buffer.get(), dataRead);

    // Download with empty version — should be a separate cache miss
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, "" /* no version */, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);
}
//---------------------------------------------------------------------------------------------------------------------