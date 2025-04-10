//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "PrefetchCacheTest.h"
#include "PrefetchCache.h"
#include "StorageInterceptLogger.h"
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
PrefetchCacheTest::PrefetchCacheTest()
    : TestBase()
{
}
//---------------------------------------------------------------------------------------------------------------------
void PrefetchCacheTest::SetUp()
{
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<FileReadData> PrefetchCacheTest::GetRandDataBuffer(off64_t offset, uint64_t size)
{
    std::shared_ptr<FileReadData> fileData = std::make_shared<FileReadData>(offset, size, nullptr);
    std::generate(fileData->Buffer.get(), fileData->Buffer.get() + size, []{return rand() % 255;});
    return fileData;
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(PrefetchCacheTest, PrefetchCachePutGetPurgeTest)
{
    PrefetchCache testCache(2, 4);
    const uint64_t chunkSize = 512;

    std::string testPath = "account1/container1/blob1";
    auto testChunk0 = GetRandDataBuffer(0, chunkSize);
    auto testChunk1 = GetRandDataBuffer(chunkSize, chunkSize);
    auto testChunk2 = GetRandDataBuffer(2*chunkSize, chunkSize);

    // No data initially
    auto cachedChunk = testCache.Get(testPath, 0);
    auto cachedChunkList = testCache.GetCachedOffsets(testPath);
    ASSERT_EQ(cachedChunk, nullptr);
    ASSERT_EQ(cachedChunkList.size(), 0);
    ASSERT_EQ(testCache.m_mapBuffers.size(), 0);

    // Put 1 chunk
    testCache.Put(testPath, testChunk0);
    cachedChunk = testCache.Get(testPath, 0);
    cachedChunkList = testCache.GetCachedOffsets(testPath);
    ASSERT_EQ(cachedChunkList.size(), 1);
    AssertEqArray(testChunk0->Buffer.get(), cachedChunk->Buffer.get(), chunkSize);
    ASSERT_EQ(testCache.m_mapBuffers.size(), 1);

    // Put 2 more chunks. Per file chunk limit of 2 must be adhered to.
    testCache.Put(testPath, testChunk1);
    testCache.Put(testPath, testChunk2);
    cachedChunkList = testCache.GetCachedOffsets(testPath);
    ASSERT_EQ(cachedChunkList.size(), 2);
    ASSERT_EQ(testCache.m_mapBuffers.size(), 2);

    // Purge one chunk
    testCache.PurgeOffsets(testPath, vector<off64_t>{ *cachedChunkList.rbegin() });
    cachedChunkList = testCache.GetCachedOffsets(testPath);
    ASSERT_EQ(cachedChunkList.size(), 1);
    ASSERT_EQ(testCache.m_mapBuffers.size(), 1);

    // Purge all chunks
    testCache.PurgeAll(testPath);
    cachedChunkList = testCache.GetCachedOffsets(testPath);
    ASSERT_EQ(cachedChunkList.size(), 0);
    ASSERT_EQ(testCache.m_mapBuffers.size(), 0);
}
//---------------------------------------------------------------------------------------------------------------------
