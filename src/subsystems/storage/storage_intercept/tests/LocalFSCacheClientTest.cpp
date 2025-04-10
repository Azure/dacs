//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "LocalFSCacheClientTest.h"
#include "LocalFSCacheClient.h"
#include "StorageInterceptConfig.h"
#include <algorithm>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <uuid/uuid.h>
#include <openssl/evp.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace cacheutils;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
LocalFSCacheClientTest::LocalFSCacheClientTest()
    : TestBase()
{
    uuid_t out;
    uuid_generate(out);
    char testId[37];
    uuid_unparse(out, testId);
    m_strTestId = testId;
    if (StorageInterceptConfig::GetInstance().GetCacheEnable("") && 
        StorageInterceptConfig::GetInstance().GetCacheUseLocalFS())
    {
        m_cacheClient = make_shared<LocalFSCacheClient>();
    }
}
//---------------------------------------------------------------------------------------------------------------------
void LocalFSCacheClientTest::SetUp()
{
}
//---------------------------------------------------------------------------------------------------------------------
atomic_int LocalFSCacheClientTest::m_fileCount = 0;
//---------------------------------------------------------------------------------------------------------------------
string LocalFSCacheClientTest::GetRandFilename()
{
    return "test-" + m_strTestId + "-" + std::to_string(m_fileCount++);
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<FileReadData> LocalFSCacheClientTest::GetRandDataBuffer(uint64_t size)
{
    std::shared_ptr<FileReadData> fileData = std::make_shared<FileReadData>(0, size, nullptr);
    std::generate(fileData->Buffer.get(), fileData->Buffer.get() + size, []{return rand() % 255;});
    return fileData;
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(LocalFSCacheClientTest, SimpleUploadDownloadTest)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    uint64_t dataRead = 0;
    CacheStatus cacheStatus;
    bool bRetVal;

    // Test 4MB file
    std::vector<char> outBuffer(1024*1024*4);
    string strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    // repeat not found
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    auto dataBuffer = GetRandDataBuffer(1024*1024*4);
    bRetVal = m_cacheClient->Upload("", strFileName, 0, dataBuffer);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    // repeat download
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, 0, outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 0);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(LocalFSCacheClientTest, DifferentFileSizesUploadDownloadTest)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    uint64_t dataRead = 0;
    CacheStatus cacheStatus;
    bool bRetVal;

    // 1 byte file test
    std::vector<char> outBuffer(1);
    string strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    auto dataBuffer = GetRandDataBuffer(1);
    bRetVal = m_cacheClient->Upload("", strFileName, 0, dataBuffer);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    // 4MB - 1 byte file test
    outBuffer.resize(1024*1024*4 - 1);
    strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    dataBuffer = GetRandDataBuffer(1024*1024*4 - 1);
    bRetVal = m_cacheClient->Upload("", strFileName, 0, dataBuffer);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4 - 1);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    // 4MB + 1 byte file test
    outBuffer.resize(1024*1024*4 + 1);
    strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    dataBuffer = GetRandDataBuffer(1024*1024*4 + 1);
    bRetVal = m_cacheClient->Upload("", strFileName, 0, dataBuffer);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*4 + 1);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    outBuffer.resize(1024*1024*10);
    strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    dataBuffer = GetRandDataBuffer(1024*1024*10);
    bRetVal = m_cacheClient->Upload("", strFileName, 0, dataBuffer);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1024*1024*10);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(LocalFSCacheClientTest, ZeroSizeUploadDownloadTest)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    uint64_t dataRead = 0;
    CacheStatus cacheStatus;
    bool bRetVal;
    std::vector<char> outBuffer(100);
    
    string strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    auto dataBuffer = GetRandDataBuffer(0);
    bRetVal = m_cacheClient->Upload("", strFileName, 0, dataBuffer);
    ASSERT_EQ(bRetVal, true);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(1);

    cacheStatus = m_cacheClient->DownloadSync("", strFileName, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(LocalFSCacheClientTest, ParallelUploadDownloadTest)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    uint64_t dataRead = 0;
    CacheStatus cacheStatus;
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
                this->m_cacheClient->DownloadSync(
                    "", fileNames[i], 0, 0, outBuffer[i].size(), outBuffer[i].data(), dataRead);
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
        auto t = make_unique<thread>([i, &dataBuffer, &fileNames, this]() {
            bool bRetVal = this->m_cacheClient->Upload("", fileNames[i], 0, dataBuffer[i]);
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
                this->m_cacheClient->DownloadSync("", fileNames[i], 0, 0, outBuffer[i].size(), outBuffer[i].data(), dataRead);
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