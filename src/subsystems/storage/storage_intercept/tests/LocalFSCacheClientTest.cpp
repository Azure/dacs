//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "LocalFSCacheClientTest.h"
#include "LocalFSCacheClient.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptUtils.h"
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
TEST_F(LocalFSCacheClientTest, SimpleUploadDownloadTest)
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

    // repeat not found
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

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
    ASSERT_EQ(dataRead, 0);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get(), dataRead);

    // Different version should miss — cache key includes version
    string version2 = StorageInterceptUtils::GenerateUUID();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version2, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(LocalFSCacheClientTest, DifferentFileSizesUploadDownloadTest)
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

    outBuffer.resize(1024*1024*10);
    strFileName = GetRandFilename();
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, 0, outBuffer.size(), outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

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
//---------------------------------------------------------------------------------------------------------------------
TEST_F(LocalFSCacheClientTest, ZeroSizeUploadDownloadTest)
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
TEST_F(LocalFSCacheClientTest, SubChunkOffsetDownloadTest)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    string version = StorageInterceptUtils::GenerateUUID();
    const uint64_t fileSize = 1024 * 1024 * 4;
    uint64_t dataRead = 0;
    CacheStatus cacheStatus;

    // Upload a 4MB chunk
    string strFileName = GetRandFilename();
    auto dataBuffer = GetRandDataBuffer(fileSize);
    bool bRetVal = m_cacheClient->Upload("", strFileName, version, 0, dataBuffer, false /*isWrite*/);
    ASSERT_EQ(bRetVal, true);
    sleep(1);

    // Download from startOffset=512 with length=256 — should return the correct sub-range
    const uint64_t subOffset = 512;
    const uint64_t subLength = 256;
    std::vector<char> outBuffer(subLength);
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, subOffset, subLength, outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, subLength);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get() + subOffset, dataRead);

    // Download from a larger offset near the middle of the file
    const uint64_t midOffset = fileSize / 2;
    const uint64_t midLength = 1024;
    outBuffer.resize(midLength);
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, midOffset, midLength, outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, midLength);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get() + midOffset, dataRead);

    // Download from the last byte of the file
    const uint64_t lastByteOffset = fileSize - 1;
    outBuffer.resize(1);
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, lastByteOffset, 1, outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 1);
    ASSERT_EQ(outBuffer[0], dataBuffer->Buffer.get()[lastByteOffset]);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(LocalFSCacheClientTest, SubChunkOffsetPastEOFTest)
{
    if (this->m_cacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    string version = StorageInterceptUtils::GenerateUUID();
    const uint64_t fileSize = 1024;
    uint64_t dataRead = 0;
    CacheStatus cacheStatus;

    // Upload a small 1KB chunk
    string strFileName = GetRandFilename();
    auto dataBuffer = GetRandDataBuffer(fileSize);
    bool bRetVal = m_cacheClient->Upload("", strFileName, version, 0, dataBuffer, false /*isWrite*/);
    ASSERT_EQ(bRetVal, true);
    sleep(1);

    // Download with startOffset past EOF — should return 0 bytes
    std::vector<char> outBuffer(256);
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, fileSize + 100, 256, outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 0);

    // Download where startOffset + length exceeds file size — should return partial read
    const uint64_t partialOffset = fileSize - 100;
    outBuffer.resize(256);
    cacheStatus = m_cacheClient->DownloadSync("", strFileName, version, 0, partialOffset, 256, outBuffer.data(), dataRead);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(dataRead, 100);
    AssertEqArray<char>(outBuffer.data(), dataBuffer->Buffer.get() + partialOffset, dataRead);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(LocalFSCacheClientTest, ParallelUploadDownloadTest)
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
        testThreads.push_back(std::move(t));
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
        testThreads.push_back(std::move(t));
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
        testThreads.push_back(std::move(t));
    }
    for (auto &t : testThreads)
    {
        t->join();
    }
    testThreads.clear();
}
//---------------------------------------------------------------------------------------------------------------------