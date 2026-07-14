//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
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
std::shared_ptr<FileData> PrefetchCacheTest::GetRandDataBuffer(off64_t offset, uint64_t size)
{
    std::shared_ptr<FileData> fileData = std::make_shared<FileData>(offset, size, nullptr /* userBuf */, false /* setToZeros */);
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
TEST_F(PrefetchCacheTest, PrefetchCacheVersionAwareTest)
{
    PrefetchCache testCache(4, 16);
    const uint64_t chunkSize = 512;

    std::string testPath = "account1/container1/blob1";
    std::string version1 = "v1-guid";
    std::string version2 = "v2-guid";

    auto chunkV1 = GetRandDataBuffer(0, chunkSize);
    auto chunkV2 = GetRandDataBuffer(0, chunkSize);

    // Put chunk with version1 — only visible to version1
    testCache.Put(testPath, chunkV1, version1);
    ASSERT_NE(testCache.Get(testPath, 0, version1), nullptr);
    ASSERT_EQ(testCache.Get(testPath, 0, version2), nullptr);
    ASSERT_EQ(testCache.Get(testPath, 0), nullptr);

    // Put same offset with version2 — both versions coexist
    testCache.Put(testPath, chunkV2, version2);
    ASSERT_NE(testCache.Get(testPath, 0, version1), nullptr);
    ASSERT_NE(testCache.Get(testPath, 0, version2), nullptr);
    ASSERT_EQ(testCache.m_mapBuffers.size(), 2);

    // GetCachedOffsets is version-scoped
    ASSERT_EQ(testCache.GetCachedOffsets(testPath, version1).size(), 1);
    ASSERT_EQ(testCache.GetCachedOffsets(testPath, version2).size(), 1);

    // Put a second chunk for version1 to test PurgeOffsets
    auto chunkV1Offset1 = GetRandDataBuffer(chunkSize, chunkSize);
    testCache.Put(testPath, chunkV1Offset1, version1);
    ASSERT_EQ(testCache.GetCachedOffsets(testPath, version1).size(), 2);
    ASSERT_EQ(testCache.m_mapBuffers.size(), 3);

    // PurgeOffsets with version1 only purges from v1, leaves v2 untouched
    testCache.PurgeOffsets(testPath, vector<off64_t>{(off64_t)chunkSize}, version1);
    ASSERT_EQ(testCache.GetCachedOffsets(testPath, version1).size(), 1);
    ASSERT_NE(testCache.Get(testPath, 0, version1), nullptr);
    ASSERT_EQ(testCache.Get(testPath, chunkSize, version1), nullptr);
    ASSERT_NE(testCache.Get(testPath, 0, version2), nullptr);
    ASSERT_EQ(testCache.m_mapBuffers.size(), 2);

    // PurgeAll with version1 only purges v1
    testCache.PurgeAll(testPath, version1);
    ASSERT_EQ(testCache.Get(testPath, 0, version1), nullptr);
    ASSERT_NE(testCache.Get(testPath, 0, version2), nullptr);
    ASSERT_EQ(testCache.m_mapBuffers.size(), 1);

    // PurgeAll with version2 purges the rest
    testCache.PurgeAll(testPath, version2);
    ASSERT_EQ(testCache.Get(testPath, 0, version2), nullptr);
    ASSERT_EQ(testCache.m_mapBuffers.size(), 0);
}
//---------------------------------------------------------------------------------------------------------------------
