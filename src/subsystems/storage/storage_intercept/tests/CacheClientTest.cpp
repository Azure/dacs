//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheClientTest.h"
#include "LocalFSCacheClient.h"
#include "RemoteCacheClient.h"
#include "StorageInterceptConfig.h"
#include <algorithm>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <uuid/uuid.h>
#include <openssl/evp.h>
//---------------------------------------------------------------------------------------------------------------------
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
    if (StorageInterceptConfig::GetInstance().GetCacheEnable(""))
    {
        if(StorageInterceptConfig::GetInstance().GetCacheUseLocalFS())
        {
            m_cacheClient = std::make_shared<LocalFSCacheClient>();
        }
        else
        {
            m_cacheClient = std::make_shared<RemoteCacheClient>();
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
std::shared_ptr<FileReadData> CacheClientTest::GetRandDataBuffer(uint64_t size)
{
    std::shared_ptr<FileReadData> fileData = std::make_shared<FileReadData>(0, size);
    std::generate(fileData->Buffer.get(), fileData->Buffer.get() + size, []{return rand() % 255;});
    return fileData;
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheClientTest, SimpleUploadDownloadTest)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    uint64_t dataRead = 0;
    ICacheClient::CacheStatus cacheStatus;
    bool bRetVal;

    // Test 4MB file
    std::vector<char> outBuffer(1024*1024*4);
    string strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND_LOCKED);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND);

    // repeat not found
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND);

    auto dataBuffer = GetRandDataBuffer(1024*1024*4);
    bRetVal = m_cacheClient->UploadAsync("", strFileName, dataBuffer);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    // repeat download
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
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

    uint64_t dataRead = 0;
    ICacheClient::CacheStatus cacheStatus;
    bool bRetVal;

    // 1 byte file test
    std::vector<char> outBuffer(1);
    string strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND_LOCKED);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND);

    auto dataBuffer = GetRandDataBuffer(1);
    bRetVal = m_cacheClient->UploadAsync("", strFileName, dataBuffer);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
    ASSERT_EQ(dataRead, 1);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    // 4MB - 1 byte file test
    outBuffer.resize(1024*1024*4 - 1);
    strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND_LOCKED);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND);

    dataBuffer = GetRandDataBuffer(1024*1024*4 - 1);
    bRetVal = m_cacheClient->UploadAsync("", strFileName, dataBuffer);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4 - 1);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    // 4MB + 1 byte file test
    outBuffer.resize(1024*1024*4 + 1);
    strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND_LOCKED);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND);

    dataBuffer = GetRandDataBuffer(1024*1024*4 + 1);
    bRetVal = m_cacheClient->UploadAsync("", strFileName, dataBuffer);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4 + 1);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    // 10MB file test
    // We currently don't support > 4MB upload/downloads when using Infiniband
    if (!StorageInterceptConfig::GetInstance().GetCacheIBEnabled())
    {
        outBuffer.resize(1024*1024*10);
        strFileName = GetRandFilename();
        cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
        ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND_LOCKED);

        cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
        ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND);

        dataBuffer = GetRandDataBuffer(1024*1024*10);
        bRetVal = m_cacheClient->UploadAsync("", strFileName, dataBuffer);
        ASSERT_EQ(bRetVal, true);

        // wait for async upload to complete 
        // TODO: make this deterministic
        sleep(1);

        cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
        ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
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

    uint64_t dataRead = 0;
    ICacheClient::CacheStatus cacheStatus;
    bool bRetVal;
    std::vector<char> outBuffer(100);
    
    string strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND_LOCKED);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND);

    auto dataBuffer = GetRandDataBuffer(0);
    bRetVal = m_cacheClient->UploadAsync("", strFileName, dataBuffer);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
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

    uint64_t dataRead = 0;
    ICacheClient::CacheStatus cacheStatus;
    bool bRetVal;
    std::vector<char> outBuffer(1024*1024*4);

    string strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND_LOCKED);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND);

    auto dataBuffer = GetRandDataBuffer(1024*1024*4);
    bRetVal = m_cacheClient->UploadAsync("", strFileName, dataBuffer);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 5, outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
    ASSERT_EQ(dataRead, 5);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 5, 25, outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
    ASSERT_EQ(dataRead, 25);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get() + 5, dataRead);

    cacheStatus = m_cacheClient->DownloadSync(
        "",
        strFileName,
        1024*1024*4 - 1,
        outBuffer.size(),
        outBuffer.data(),
        dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
    ASSERT_EQ(dataRead, 1);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get() + 1024*1024*4 - 1, dataRead);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 1024*1024*4, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
    ASSERT_EQ(dataRead, 0);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 999, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
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

    uint64_t dataRead = 0;
    ICacheClient::CacheStatus cacheStatus;
    bool bRetVal;

    // Create many files of different file sizes
    int numFiles = 200;
    string strFileName = GetRandFilename();
    vector<std::shared_ptr<FileReadData>> dataBuffer(numFiles);
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
        auto t = make_unique<thread>([i, &outBuffer, &fileNames, this]() {
            uint64_t dataRead = 0;
            auto cacheStatus =
                this->m_cacheClient->DownloadSync("", fileNames[i], 0, outBuffer[i].size(), outBuffer[i].data(), dataRead);
            ASSERT_EQ(cacheStatus, ICacheClient::NOTFOUND_LOCKED);
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
        auto t = make_unique<thread>([i, &dataBuffer, &fileNames, this]() {
            bool bRetVal = this->m_cacheClient->UploadAsync("", fileNames[i], dataBuffer[i]);
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
        auto t = make_unique<thread>([i, &dataBuffer, &outBuffer, &fileNames, this]() {
            uint64_t dataRead = 0;
            auto cacheStatus =
                this->m_cacheClient->DownloadSync("", fileNames[i], 0, outBuffer[i].size(), outBuffer[i].data(), dataRead);
            ASSERT_EQ(cacheStatus, ICacheClient::SUCCESS);
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