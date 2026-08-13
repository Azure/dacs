// Copyright (c) Microsoft Corporation.
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

#include "StreamingClientTest.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptConfig.h"
#include "StreamingClient.h"
#include "InMemoryAttributesCache.h"
#include "RemoteCacheClient.h"
#include <atomic>
#include <map>
#include <vector>
#include <mutex>
#include <thread>
#include <dirent.h>
//---------------------------------------------------------------------------------------------------------------------
#define ASSERT_IF(condition, stmt)                                                         { if (condition) { stmt; } }
//---------------------------------------------------------------------------------------------------------------------
// In-memory cache client for testing the cache-server sub-chunk download path.
// Tracks all DownloadSync calls so tests can verify startOffset/length parameters.
//---------------------------------------------------------------------------------------------------------------------
class InMemoryCacheClient : public ICacheClient
{
public:
    struct DownloadCall {
        std::string filePath;
        std::string version;
        off64_t fileOffset;
        uint64_t startOffset;
        uint64_t length;
    };

    struct UploadCall {
        std::string filePath;
        std::string version;
        off64_t fileOffset;
        bool isWrite;
    };

    cacheutils::CacheStatus DownloadSync(
        const std::string& /*mountName*/,
        const std::string& filePath,
        const std::string& version,
        off64_t fileOffset,
        uint64_t startOffset,
        uint64_t length,
        char* buffer,
        uint64_t& readLength) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_downloadCalls.push_back({filePath, version, fileOffset, startOffset, length});

        // Include version in store key so different versions have separate cache entries
        std::string key = filePath + ":" + version + ":" + std::to_string(fileOffset);
        auto it = m_store.find(key);
        if (it == m_store.end())
            return cacheutils::CacheStatus::NOTFOUND_LOCKED;

        auto& data = it->second;
        if (startOffset >= data->BufferSize)
        {
            readLength = 0;
            return cacheutils::CacheStatus::SUCCESS;
        }
        uint64_t available = data->BufferSize - startOffset;
        uint64_t toCopy = std::min(length, available);
        memcpy(buffer, data->Buffer.get() + startOffset, toCopy);
        readLength = toCopy;
        return cacheutils::CacheStatus::SUCCESS;
    }

    bool Upload(
        const std::string& /*mountName*/,
        const std::string& filePath,
        const std::string& version,
        off64_t fileOffset,
        std::shared_ptr<const FileData> fileData,
        bool isWrite) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string key = filePath + ":" + version + ":" + std::to_string(fileOffset);
        m_store[key] = fileData;
        m_uploadCalls.push_back({filePath, version, fileOffset, isWrite});
        return true;
    }

    bool DeleteGroup(const std::string& groupId) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deleteGroupCalls.push_back(groupId);
        return true;
    }

    mutable std::mutex m_mutex; // Guards m_store, m_downloadCalls, m_uploadCalls against concurrent std::async calls in WriteChunksToCache
    std::vector<DownloadCall> m_downloadCalls;
    std::vector<UploadCall> m_uploadCalls;
    std::vector<std::string> m_deleteGroupCalls;
    std::map<std::string, std::shared_ptr<const FileData>> m_store;
};
//---------------------------------------------------------------------------------------------------------------------
void StreamingClientTest::EnsureSubChunkConfig()
{
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheEncryptionEnabled, "false");
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheValidateChecksumEnabled, "false");
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableZeroCopy, "false");

    m_spStreamingClient.reset();
    SetUp();
}
//---------------------------------------------------------------------------------------------------------------------
StreamingClientTest::StreamingClientTest()
    : TestBase(), m_vecBuffer(m_vecFileBuf)
{
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClientTest::SetUp()
{
    const bool isRemoteCacheEnabled = StorageInterceptConfig::GetInstance().GetCacheEnable("") && 
        !StorageInterceptConfig::GetInstance().GetCacheUseLocalFS();
    if (isRemoteCacheEnabled)
    {
        m_cacheClient = 
            std::make_shared<RemoteCacheClient>(StorageInterceptConfig::GetInstance().GetStreamingChunkSize());
    }

    m_spStreamingClient = std::make_shared<StreamingClient>(
        "",
        m_cacheClient,
        std::make_shared<InMemoryAttributesCache>());

    // Get the file version for the test file (etag or dacsfileversion) — used by all read tests
    FileAttributes setupAttrs;
    if (m_spStreamingClient->GetProperties(m_strRemoteFilePath, setupAttrs) == 0)
    {
        m_strFileVersion = setupAttrs.version;
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool StreamingClientTest::BufferEqualsInRange(
    const std::vector<char>& vecFirst,
    const std::vector<char>& vecSecond,
    size_t nStartIdxFirst,
    size_t nStartIdxSecond,
    size_t nSize)
{
    bool bIdentical = true;
    if ( (nStartIdxFirst + nSize) > vecFirst.size() || (nStartIdxSecond + nSize) > vecSecond.size())
    {
        bIdentical = false;
        return bIdentical;
    }
    for(size_t idx = 0; idx < nSize; idx++)
    {
        if (vecFirst[nStartIdxFirst + idx] != vecSecond[nStartIdxSecond + idx])
        {
            bIdentical = false;
            return bIdentical;
        }
    }
    return bIdentical;
}
//---------------------------------------------------------------------------------------------------------------------
bool StreamingClientTest::BufferEquals(
    const std::vector<char>& vecFirst,
    const std::vector<char>& vecSecond)
{
    if (vecFirst.size() != vecSecond.size())
    {
        return false;
    }
    bool bResult = BufferEqualsInRange(vecFirst, vecSecond, 0, 0, vecFirst.size());
    return bResult;
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClientTest::PerformReadTest(size_t bufferSize,
                                          size_t offset,
                                          ssize_t expectedBytesRead,
                                          int expectedZeroCopyReads)
{
    std::vector<char> readBuf(bufferSize);

    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath,
        readBuf.data(),
        readBuf.size(),
        offset, /* offset */
        REMOTE_FILE_SIZE,
        m_strFileVersion);
    ASSERT_EQ(nBytesRead, expectedBytesRead);
    // Data integrity check
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, offset, 0, nBytesRead)));

    ASSERT_EQ(m_spStreamingClient->GetReadChunksZeroCopy(), expectedZeroCopyReads);
    // Close the file, forcing to purge the cache
    m_spStreamingClient->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, RootDirPathTest)
{
    const std::string rootDirPath = "";
    std::string continuationToken;
    std::vector<DirectoryEntry> dirEntries;

    /* Tests if ReadDirectory call on root path works correctly */
    continuationToken = m_spStreamingClient->ReadDirectory(
        rootDirPath,
        continuationToken,
        dirEntries /* [out] dirents */
    );
    ASSERT_TRUE(dirEntries.size() > 0);

    /* Verify version is populated for file entries (etag fallback or dacsfileversion) */
    for (const auto& entry : dirEntries)
    {
        if (entry.d_type == DT_REG)
        {
            ASSERT_NE(entry.d_version[0], '\0');
        }
    }

    /* Tests if GetProperties call on root path, identifies it as a valid directory */
    FileAttributes attr;
    int nResult = m_spStreamingClient->GetProperties(
        rootDirPath,
        attr
    );
    ASSERT_EQ(nResult, 0);
    ASSERT_EQ(attr.isDir, true);
}
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
// BEGIN: Test cases for streaming client with zero copy disabled
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadResetStreamingClientDisableZC)
{
    // Save the current streaming client instance
    auto originalStreamingClient = m_spStreamingClient;

    // Override the config to enable zero copy and reset the m_spStreamingClient
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableZeroCopy, "false");

    // Reset the streaming client
    m_spStreamingClient.reset();

    // Reinitialize the streaming client
    SetUp();

    // Since the new streaming client is created, don't wanna reset the config again
    m_bConfigUpdated = false;

    // Validate that the streaming client instance has changed
    ASSERT_NE(originalStreamingClient, m_spStreamingClient);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFilePartialTest)
{
    // Read from 10th offset
    PerformReadTest(REMOTE_FILE_SIZE / 2, 10, REMOTE_FILE_SIZE / 2, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadChunkFullFileTest)
{
    // Read full file
    PerformReadTest(REMOTE_FILE_SIZE, 0, REMOTE_FILE_SIZE, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileTinyReadsTest)
{
    // Read first 50 bytes.
    PerformReadTest(50, 0, 50, 0);

    // Read next 50 bytes.
    PerformReadTest(50, 50, 50, 0);

    // Read next 50 bytes.
    PerformReadTest(50, 100, 50, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeLargerThanFileSizeTest)
{
    // Read larger than file size
    PerformReadTest(2 * REMOTE_FILE_SIZE, 0, REMOTE_FILE_SIZE, 0);
}
//---------------------------------------------------------------------------------------------------------------------
// END: Test cases for streaming client with zero copy disabled
//---------------------------------------------------------------------------------------------------------------------
// BEGIN: Test cases for streaming client with zero copy enabled
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadResetStreamingClientEnableZC)
{
    // Save the current streaming client instance
    auto originalStreamingClient = m_spStreamingClient;

    // Override the config to enable zero copy and reset the m_spStreamingClient
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableZeroCopy, "true");

    // Reset the streaming client
    m_spStreamingClient.reset();

    // Reinitialize the streaming client
    SetUp();

    // Since the new streaming client is created, don't wanna reset the config again
    m_bConfigUpdated = false;

    // Validate that the streaming client instance has changed
    ASSERT_NE(originalStreamingClient, m_spStreamingClient);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFilePartialTestZC)
{
    // Read from 10th offset
    PerformReadTest(REMOTE_FILE_SIZE / 2, 10, REMOTE_FILE_SIZE / 2, 1);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadChunkFullFileTestZC)
{
    // Read full file
    PerformReadTest(REMOTE_FILE_SIZE, 0, REMOTE_FILE_SIZE, 4);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileTinyReadsTestZC)
{
    // Read first 50 bytes.
    PerformReadTest(50, 0, 50, 0);

    // Read next 50 bytes.
    PerformReadTest(50, 50, 50, 0);

    // Read next 50 bytes.
    PerformReadTest(50, 100, 50, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeLargerThanFileSizeTestZC)
{
    // Read larger than file size
    PerformReadTest(2 * REMOTE_FILE_SIZE, 0, REMOTE_FILE_SIZE, 4);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeUnalignedRequest1ByteAtStartTestZC)
{
    PerformReadTest(1, 0, 1, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeUnalignedRequest10ByteAtOffset1TestZC)
{
    PerformReadTest(10, 1, 10, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeUnalignedRequestNearChunkBoundaryTestZC)
{
    PerformReadTest(DEFAULT_STREAMING_CHUNK_SIZE - 10, 0, DEFAULT_STREAMING_CHUNK_SIZE - 10, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeUnalignedRequestCrossingChunkBoundaryTestZC)
{
    PerformReadTest(DEFAULT_STREAMING_CHUNK_SIZE + 10, DEFAULT_STREAMING_CHUNK_SIZE,
                    DEFAULT_STREAMING_CHUNK_SIZE + 10, 1);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeWithinHalfChunkTestZC)
{
    PerformReadTest(DEFAULT_STREAMING_CHUNK_SIZE / 2, 0, DEFAULT_STREAMING_CHUNK_SIZE / 2, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeFullChunkTestZC)
{
    PerformReadTest(DEFAULT_STREAMING_CHUNK_SIZE, 0, DEFAULT_STREAMING_CHUNK_SIZE, 1);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeFullChunkAtOffsetTestZC)
{
    PerformReadTest(DEFAULT_STREAMING_CHUNK_SIZE, DEFAULT_STREAMING_CHUNK_SIZE,
                    DEFAULT_STREAMING_CHUNK_SIZE, 1);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeMultipleChunksTestZC)
{
    PerformReadTest(2 * DEFAULT_STREAMING_CHUNK_SIZE, 0, 2 * DEFAULT_STREAMING_CHUNK_SIZE, 2);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeThreeChunksTestZC)
{
    PerformReadTest(3 * DEFAULT_STREAMING_CHUNK_SIZE, 0, 3 * DEFAULT_STREAMING_CHUNK_SIZE, 3);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeFourChunksTestZC)
{
    PerformReadTest(4 * DEFAULT_STREAMING_CHUNK_SIZE, 0, REMOTE_FILE_SIZE, 4);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeChunkPlusExtraBytesTestZC)
{
    PerformReadTest(DEFAULT_STREAMING_CHUNK_SIZE + 50, 0, DEFAULT_STREAMING_CHUNK_SIZE + 50, 1);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeTwoChunksPlusExtraBytesTestZC)
{
    PerformReadTest(2 * DEFAULT_STREAMING_CHUNK_SIZE + 50, 0, 2 * DEFAULT_STREAMING_CHUNK_SIZE + 50, 2);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeTwoChunksPlusExtraBytesAtOffsetTestZC)
{
    PerformReadTest(2 * DEFAULT_STREAMING_CHUNK_SIZE + 50, DEFAULT_STREAMING_CHUNK_SIZE,
                    2 * DEFAULT_STREAMING_CHUNK_SIZE + 50, 2);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeMoreThanFileSizeTestZC)
{
    PerformReadTest(6 * DEFAULT_STREAMING_CHUNK_SIZE, 0, REMOTE_FILE_SIZE, 4);
}
//---------------------------------------------------------------------------------------------------------------------
// END: Test cases for streaming client with zero copy enabled
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
// START: Test cases for streaming client with sub-chunk read optimization
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileTinyReadsSubChunkTest)
{
    EnsureSubChunkConfig();
    uint64_t subChunkBefore = m_spStreamingClient->GetReadChunksSubChunk();

    // First read — small read falls through sub-chunk (PrefetchCache miss, no cache
    // server in serverless CI) to the full-chunk path, which populates PrefetchCache.
    std::vector<char> readBuf(50);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 50);
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, 0, 0, nBytesRead)));

    // No Close() — keep PrefetchCache populated so the next read can hit sub-chunk.
    uint64_t subChunkAfterWarmup = m_spStreamingClient->GetReadChunksSubChunk();

    // Second read of same region should be a sub-chunk PrefetchCache hit
    nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 50);
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, 0, 0, nBytesRead)));
    ASSERT_EQ(m_spStreamingClient->GetReadChunksSubChunk() - subChunkAfterWarmup, 1u);
    ASSERT_GE(m_spStreamingClient->GetReadChunksSubChunk() - subChunkBefore, 1u);
    m_spStreamingClient->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileSubChunkAtOffsetTest)
{
    EnsureSubChunkConfig();
    uint64_t subChunkBefore = m_spStreamingClient->GetReadChunksSubChunk();

    // First read to populate PrefetchCache for chunk 0
    std::vector<char> readBuf(512);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 100, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 512);

    // No Close() — keep PrefetchCache populated
    uint64_t subChunkAfterWarmup = m_spStreamingClient->GetReadChunksSubChunk();

    // Second read at the same offset should hit sub-chunk PrefetchCache path
    nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 100, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 512);
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, 100, 0, nBytesRead)));
    ASSERT_EQ(m_spStreamingClient->GetReadChunksSubChunk() - subChunkAfterWarmup, 1u);
    ASSERT_GE(m_spStreamingClient->GetReadChunksSubChunk() - subChunkBefore, 1u);
    m_spStreamingClient->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileFullChunkBypassesSubChunkTest)
{
    EnsureSubChunkConfig();
    uint64_t subChunkBefore = m_spStreamingClient->GetReadChunksSubChunk();

    // A full-chunk-sized read should NOT use the sub-chunk path
    std::vector<char> readBuf(DEFAULT_STREAMING_CHUNK_SIZE);

    // First read to populate cache
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, static_cast<ssize_t>(DEFAULT_STREAMING_CHUNK_SIZE));
    m_spStreamingClient->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);

    // Second read — full chunk size is not less than m_nChunkSize, should not use sub-chunk
    nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, static_cast<ssize_t>(DEFAULT_STREAMING_CHUNK_SIZE));
    ASSERT_EQ(m_spStreamingClient->GetReadChunksSubChunk() - subChunkBefore, 0u);
    m_spStreamingClient->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileSubChunkDisabledWithEncryptionTest)
{
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheEncryptionEnabled, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheValidateChecksumEnabled, "false");
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableZeroCopy, "false");
    m_spStreamingClient.reset();
    SetUp();

    uint64_t subChunkBefore = m_spStreamingClient->GetReadChunksSubChunk();

    // First small read to populate PrefetchCache
    std::vector<char> readBuf(50);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 50);

    // Second read — sub-chunk should NOT activate when encryption is enabled
    nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 50);
    ASSERT_EQ(m_spStreamingClient->GetReadChunksSubChunk() - subChunkBefore, 0u);
    m_spStreamingClient->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileSubChunkDisabledWithChecksumTest)
{
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheEncryptionEnabled, "false");
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheValidateChecksumEnabled, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableZeroCopy, "false");
    m_spStreamingClient.reset();
    SetUp();

    uint64_t subChunkBefore = m_spStreamingClient->GetReadChunksSubChunk();

    // First small read to populate PrefetchCache
    std::vector<char> readBuf(50);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 50);

    // Second read — sub-chunk should NOT activate when checksum validation is enabled
    nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 50);
    ASSERT_EQ(m_spStreamingClient->GetReadChunksSubChunk() - subChunkBefore, 0u);
    m_spStreamingClient->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileMultipleSubChunkHitsTest)
{
    EnsureSubChunkConfig();
    uint64_t subChunkBefore = m_spStreamingClient->GetReadChunksSubChunk();

    // Warmup read to populate PrefetchCache for chunk 0
    std::vector<char> readBuf(256);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 256);

    // No Close() — keep PrefetchCache populated
    uint64_t subChunkAfterWarmup = m_spStreamingClient->GetReadChunksSubChunk();

    // Multiple small reads at different offsets within the same cached chunk
    for (int i = 0; i < 5; i++)
    {
        size_t offset = i * 256;
        nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), offset, REMOTE_FILE_SIZE, m_strFileVersion);
        ASSERT_EQ(nBytesRead, 256);
        ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, offset, 0, nBytesRead)));
    }

    // All 5 reads should have been sub-chunk PrefetchCache hits
    ASSERT_EQ(m_spStreamingClient->GetReadChunksSubChunk() - subChunkAfterWarmup, 5u);
    ASSERT_GE(m_spStreamingClient->GetReadChunksSubChunk() - subChunkBefore, 5u);
    m_spStreamingClient->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileSubChunkPrefetchCacheHitTest)
{
    EnsureSubChunkConfig();
    // Verify the sub-chunk path serves from PrefetchCache when the file is still open.
    // Without Close() between reads, PrefetchCache retains the chunk from the first read.
    uint64_t subChunkBefore = m_spStreamingClient->GetReadChunksSubChunk();
    uint64_t prefetchBefore = m_spStreamingClient->GetReadChunksPrefetchCache();

    // First read — use full chunk size to force the full-chunk path (bypasses sub-chunk
    // since bufSize >= threshold) and populate PrefetchCache.
    std::vector<char> warmBuf(DEFAULT_STREAMING_CHUNK_SIZE);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, warmBuf.data(), warmBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, static_cast<ssize_t>(DEFAULT_STREAMING_CHUNK_SIZE));

    uint64_t subChunkAfterFirst = m_spStreamingClient->GetReadChunksSubChunk();

    // No Close() — PrefetchCache still holds the chunk

    // Second small read on the same chunk — should hit PrefetchCache sub-chunk path
    std::vector<char> readBuf(128);
    nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 128);
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, 0, 0, nBytesRead)));

    // Sub-chunk counter should have incremented by 1 from the second read
    ASSERT_EQ(m_spStreamingClient->GetReadChunksSubChunk() - subChunkAfterFirst, 1u);
    // PrefetchCache counter should have incremented (from the sub-chunk prefetch-cache hit)
    ASSERT_GT(m_spStreamingClient->GetReadChunksPrefetchCache(), prefetchBefore);

    m_spStreamingClient->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileSubChunkCacheServerPathTest)
{
    EnsureSubChunkConfig();

    // Enable cache so SetClients stores our mock cache client (in serverless CI
    // environments GetCacheEnable() defaults to false).
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheEnable, "true");

    // Create a StreamingClient backed by an in-memory cache to exercise the
    // cache-server sub-chunk download path (PrefetchCache miss → DownloadSync
    // with startOffset/length).
    auto mockCache = std::make_shared<InMemoryCacheClient>();
    auto client = std::make_shared<StreamingClient>(
        "", mockCache, std::make_shared<InMemoryAttributesCache>());

    // Purge any stale PrefetchCache entries from prior tests for this file
    client->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);

    // Warmup: full-chunk read populates the in-memory cache via Upload
    std::vector<char> warmBuf(DEFAULT_STREAMING_CHUNK_SIZE);
    ssize_t nBytesRead = client->ReadFile(
        m_strRemoteFilePath, warmBuf.data(), warmBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, static_cast<ssize_t>(DEFAULT_STREAMING_CHUNK_SIZE));

    // Close purges PrefetchCache so the next read must go to the cache server
    client->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);

    size_t callsBefore = mockCache->m_downloadCalls.size();
    uint64_t subChunkBefore = client->GetReadChunksSubChunk();

    // Small sub-chunk read at offset 100, size 512
    std::vector<char> readBuf(512);
    nBytesRead = client->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 100, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 512);

    // Verify the cache server received a sub-chunk download request
    ASSERT_GT(mockCache->m_downloadCalls.size(), callsBefore);
    auto& lastCall = mockCache->m_downloadCalls.back();
    ASSERT_EQ(lastCall.startOffset, 100u);
    ASSERT_EQ(lastCall.length, 512u);

    // Verify the sub-chunk counter incremented
    ASSERT_EQ(client->GetReadChunksSubChunk() - subChunkBefore, 1u);

    // Verify data correctness
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, 100, 0, 512)));

    client->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileSubChunkColdPathTest)
{
    EnsureSubChunkConfig();

    // Enable cache so SetClients stores our mock cache client
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheEnable, "true");

    // Create a StreamingClient with an EMPTY in-memory cache — every chunk is a cold miss.
    // The sub-chunk probe will return NOTFOUND_LOCKED (we get the lock).
    // The fix should: download full chunk from blob, upload to cache server, extract sub-chunk.
    auto mockCache = std::make_shared<InMemoryCacheClient>();
    auto client = std::make_shared<StreamingClient>(
        "", mockCache, std::make_shared<InMemoryAttributesCache>());

    // Purge any stale PrefetchCache entries
    client->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);

    size_t uploadsBefore = mockCache->m_uploadCalls.size();
    uint64_t subChunkBefore = client->GetReadChunksSubChunk();

    // Small sub-chunk read at offset 100, size 512 — cache is empty (cold)
    std::vector<char> readBuf(512);
    ssize_t nBytesRead = client->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 100, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 512);

    // Verify the sub-chunk counter incremented (cold path still counts as sub-chunk)
    ASSERT_EQ(client->GetReadChunksSubChunk() - subChunkBefore, 1u);

    // Verify the cache server received an Upload (the lock was used correctly)
    ASSERT_GT(mockCache->m_uploadCalls.size(), uploadsBefore);

    // Verify data correctness
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, 100, 0, 512)));

    // Verify cache is now populated — a second sub-chunk read should hit the cache
    client->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);  // purge PrefetchCache
    size_t callsBefore = mockCache->m_downloadCalls.size();

    std::vector<char> readBuf2(256);
    nBytesRead = client->ReadFile(
        m_strRemoteFilePath, readBuf2.data(), readBuf2.size(), 200, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 256);

    // This second read should hit the cache (SUCCESS, not NOTFOUND_LOCKED)
    ASSERT_GT(mockCache->m_downloadCalls.size(), callsBefore);
    auto& secondCall = mockCache->m_downloadCalls.back();
    ASSERT_EQ(secondCall.startOffset, 200u);
    ASSERT_EQ(secondCall.length, 256u);

    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf2, 200, 0, 256)));

    client->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileSubChunkZeroByteTest)
{
    EnsureSubChunkConfig();

    // Warmup: populate PrefetchCache for chunk 0
    std::vector<char> warmBuf(DEFAULT_STREAMING_CHUNK_SIZE);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, warmBuf.data(), warmBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, static_cast<ssize_t>(DEFAULT_STREAMING_CHUNK_SIZE));

    // Zero-byte read — should return 0 without crashing
    char dummy = 0;
    nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, &dummy, 0, 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 0);

    m_spStreamingClient->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileSubChunkNearEOFTest)
{
    EnsureSubChunkConfig();

    // Warmup: read at offset near the last chunk to populate PrefetchCache for chunk 3
    const size_t lastChunkOffset = (REMOTE_FILE_SIZE / DEFAULT_STREAMING_CHUNK_SIZE - 1)
                                   * DEFAULT_STREAMING_CHUNK_SIZE;
    std::vector<char> warmBuf(DEFAULT_STREAMING_CHUNK_SIZE);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, warmBuf.data(), warmBuf.size(), lastChunkOffset, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, static_cast<ssize_t>(DEFAULT_STREAMING_CHUNK_SIZE));

    uint64_t subChunkBefore = m_spStreamingClient->GetReadChunksSubChunk();

    // Read near EOF: request 512 bytes but only 100 bytes remain
    const size_t nearEOFOffset = REMOTE_FILE_SIZE - 100;
    std::vector<char> readBuf(512);
    nBytesRead = m_spStreamingClient->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), nearEOFOffset, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, 100);
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, nearEOFOffset, 0, nBytesRead)));

    // The read should have taken the sub-chunk PrefetchCache path
    ASSERT_EQ(m_spStreamingClient->GetReadChunksSubChunk() - subChunkBefore, 1u);

    m_spStreamingClient->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileSubChunkBoundaryStraddleTest)
{
    EnsureSubChunkConfig();

    // Enable cache so SetClients stores our mock cache client
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheEnable, "true");

    // Create a StreamingClient backed by an in-memory cache to verify that a
    // small read straddling two chunks issues sub-chunk downloads for both.
    auto mockCache = std::make_shared<InMemoryCacheClient>();
    auto client = std::make_shared<StreamingClient>(
        "", mockCache, std::make_shared<InMemoryAttributesCache>());

    // Purge any stale PrefetchCache entries
    client->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);

    // Warmup: read chunk 0 and chunk 1 fully to populate the in-memory cache
    std::vector<char> warmBuf(DEFAULT_STREAMING_CHUNK_SIZE);
    ssize_t nBytesRead = client->ReadFile(
        m_strRemoteFilePath, warmBuf.data(), warmBuf.size(), 0, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, static_cast<ssize_t>(DEFAULT_STREAMING_CHUNK_SIZE));

    nBytesRead = client->ReadFile(
        m_strRemoteFilePath, warmBuf.data(), warmBuf.size(), DEFAULT_STREAMING_CHUNK_SIZE, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, static_cast<ssize_t>(DEFAULT_STREAMING_CHUNK_SIZE));

    // Close purges PrefetchCache — next read must go through the cache server
    client->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);

    size_t downloadsBefore = mockCache->m_downloadCalls.size();
    size_t uploadsBefore   = mockCache->m_uploadCalls.size();
    uint64_t subChunkBefore = client->GetReadChunksSubChunk();

    // Read 4KB straddling the chunk boundary: 2KB from end of chunk 0, 2KB from start of chunk 1
    constexpr size_t straddleReadSize = 4096;
    constexpr size_t overlap = 2048;
    const off64_t straddleOffset = DEFAULT_STREAMING_CHUNK_SIZE - overlap;

    std::vector<char> readBuf(straddleReadSize);
    nBytesRead = client->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), straddleOffset, REMOTE_FILE_SIZE, m_strFileVersion);
    ASSERT_EQ(nBytesRead, static_cast<ssize_t>(straddleReadSize));

    // Verify data correctness
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(
        m_vecBuffer, readBuf, straddleOffset, 0, straddleReadSize)));

    // Exactly 2 new DownloadSync calls — one per chunk
    ASSERT_EQ(mockCache->m_downloadCalls.size(), downloadsBefore + 2);

    // First call: tail of chunk 0
    auto& call0 = mockCache->m_downloadCalls[downloadsBefore];
    ASSERT_EQ(call0.fileOffset, 0);
    ASSERT_EQ(call0.startOffset, static_cast<uint64_t>(DEFAULT_STREAMING_CHUNK_SIZE - overlap));
    ASSERT_EQ(call0.length, static_cast<uint64_t>(overlap));

    // Second call: head of chunk 1
    auto& call1 = mockCache->m_downloadCalls[downloadsBefore + 1];
    ASSERT_EQ(call1.fileOffset, static_cast<off64_t>(DEFAULT_STREAMING_CHUNK_SIZE));
    ASSERT_EQ(call1.startOffset, 0u);
    ASSERT_EQ(call1.length, static_cast<uint64_t>(overlap));

    // No new uploads — both chunks were already cached (warm path)
    ASSERT_EQ(mockCache->m_uploadCalls.size(), uploadsBefore);

    // Sub-chunk counter incremented by 2 (one per chunk portion)
    ASSERT_EQ(client->GetReadChunksSubChunk() - subChunkBefore, 2u);

    client->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
// END: Test cases for streaming client with sub-chunk read optimization
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
// START: Test cases for writes
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ResetStreamingClientForWriteTests)
{
    // Save the current streaming client instance
    auto originalStreamingClient = m_spStreamingClient;

    const bool isCacheEnabled = m_cacheClient != nullptr;

    // Ensure write support is enabled if using mock or running against cache
    if (m_bUsingMock || isCacheEnabled)
    {
        OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnable, "true");
    }
    // Disable async remote writes if cache is not enabled
    if (!isCacheEnabled)
    {
        OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnableRemoteAsync, "false");
    }
    // Disable remote(blob) writes if not using mock as pipeline auth doesnt give blob write permissions.
    if (!m_bUsingMock)
    {
        OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnableRemote, "false");
    }

    // Reset the streaming client
    m_spStreamingClient.reset();

    // Reinitialize the streaming client
    SetUp();

    // Since the new streaming client is created, don't wanna reset the config again
    m_bConfigUpdated = false;

    // Validate that the streaming client instance has changed
    ASSERT_NE(originalStreamingClient, m_spStreamingClient);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, WriteWholeFileTest)
{
    if (!StorageInterceptConfig::GetInstance().IsWriteSupportEnabled())
    {
        GTEST_SKIP();
    }

    auto writeData = GetRandDataBuffer(REMOTE_FILE_SIZE);
    std::string writeFileName = "wholeFileWriteTest" + GenerateRandomString(4);
    ssize_t nBytesWritten = m_spStreamingClient->WriteFile(
        writeFileName,
        writeData->Buffer.get(),
        writeData->BufferSize,
        0 /* offset */);
    ASSERT_EQ(nBytesWritten, writeData->BufferSize);

    // Close the file to flush the writes
    m_spStreamingClient->Close(writeFileName, "" /* version */, true /* isWriteMode */);

    // Get the generated version for the written file
    FileAttributes writeAttrs;
    m_spStreamingClient->GetProperties(writeFileName, writeAttrs);
    ASSERT_FALSE(writeAttrs.version.empty());

    // Read back the file to verify data integrity — use the generated version
    std::shared_ptr<FileData> readData = std::make_shared<FileData>(
        0,
        writeData->BufferSize,
        nullptr,
        false /* setToZeros */);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        writeFileName, readData->Buffer.get(), readData->BufferSize, 0 /* offset */, REMOTE_FILE_SIZE, writeAttrs.version);
    ASSERT_EQ(nBytesRead, writeData->BufferSize);
    ASSERT_TRUE(FileDataEquals(writeData, readData));

    // Close the file
    m_spStreamingClient->Close(writeFileName, "" /* version */, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClientTest::WriteAppendsTest(
    const std::string& remotePath,
    std::shared_ptr<FileData> writeData,
    size_t appendSize)
{
    ssize_t totalBytesWritten = 0;
    for(size_t offset = 0; offset < writeData->BufferSize; offset += appendSize)
    {
        const size_t writeSize = std::min(appendSize, writeData->BufferSize - offset); // Last append may be smaller
        ssize_t nBytesWritten = m_spStreamingClient->WriteFile(
            remotePath,
            writeData->Buffer.get() + offset,
            writeSize,
            offset /* offset */);
        ASSERT_EQ(nBytesWritten, writeSize);
        totalBytesWritten += nBytesWritten;
    }
    ASSERT_EQ(totalBytesWritten, writeData->BufferSize);

    // Close the file to flush the writes
    m_spStreamingClient->Close(remotePath, "" /* version */, true /* isWriteMode */);

    // Get properties to verify file size updated and version generated
    FileAttributes attributes;
    int nResult = m_spStreamingClient->GetProperties(
        remotePath,
        attributes /* [out] attributes */);
    ASSERT_EQ(nResult, 0);
    ASSERT_EQ(attributes.size, writeData->BufferSize);
    ASSERT_FALSE(attributes.version.empty());

    // Read back the file to verify data integrity — use the generated version
    std::shared_ptr<FileData> readData = std::make_shared<FileData>(
        0,
        writeData->BufferSize,
        nullptr,
        false /* setToZeros */);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        remotePath,
        readData->Buffer.get(),
        readData->BufferSize,
        0 /* offset */,
        writeData->BufferSize,
        attributes.version);
    ASSERT_EQ(nBytesRead, writeData->BufferSize);
    ASSERT_TRUE(FileDataEquals(writeData, readData));
    m_spStreamingClient->Close(remotePath, "" /* version */, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, WriteFileAppendsAtChunkBoundaryTest)
{
    if (!StorageInterceptConfig::GetInstance().IsWriteSupportEnabled())
    {
        GTEST_SKIP();
    }

    auto writeData = GetRandDataBuffer(REMOTE_FILE_SIZE);
    std::string writeFileName = "fileAppendsTest" + GenerateRandomString(4);

    // Perform multiple appends and close
    WriteAppendsTest(
        writeFileName,
        writeData,
        REMOTE_FILE_SIZE / 4 /* appendSize */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, WriteFileTinyAppendsTest)
{
    if (!StorageInterceptConfig::GetInstance().IsWriteSupportEnabled())
    {
        GTEST_SKIP();
    }

    auto writeData = GetRandDataBuffer(REMOTE_FILE_SIZE + 1024); // To test file size not a multiple of chunk size
    std::string writeFileName = "fileTinyAppendsTest" + GenerateRandomString(4);

    // Perform multiple tiny appends and close
    WriteAppendsTest(
        writeFileName,
        writeData,
        10 * 1024 /* appendSize such that it would not align with 4M chunk boundaries */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, WriteSparseFileTest)
{
    if (!StorageInterceptConfig::GetInstance().IsWriteSupportEnabled())
    {
        GTEST_SKIP();
    }

    auto writeData = GetRandDataBuffer(REMOTE_FILE_SIZE / 2);
    std::string writeFileName = "sparseFileWriteTest" + GenerateRandomString(4);

    // Write first half of the file
    ssize_t nBytesWritten = m_spStreamingClient->WriteFile(
        writeFileName,
        writeData->Buffer.get(),
        writeData->BufferSize,
        0 /* offset */);
    ASSERT_EQ(nBytesWritten, writeData->BufferSize);

    // Write second half of the file, leaving a hole in between
    const size_t holeSize = REMOTE_FILE_SIZE / 4;
    ssize_t nBytesWrittenSecondHalf = m_spStreamingClient->WriteFile(
        writeFileName,
        writeData->Buffer.get() + writeData->BufferSize / 2,
        writeData->BufferSize / 2,
        writeData->BufferSize + holeSize /* offset */);
    ASSERT_EQ(nBytesWrittenSecondHalf, writeData->BufferSize / 2);

    // Close the file to flush the writes
    m_spStreamingClient->Close(writeFileName, "" /* version */, true /* isWriteMode */);

    // Get the generated version for the written file
    FileAttributes sparseAttrs;
    m_spStreamingClient->GetProperties(writeFileName, sparseAttrs);
    ASSERT_FALSE(sparseAttrs.version.empty());

    // Read back the file to verify data integrity
    std::shared_ptr<FileData> readData = std::make_shared<FileData>(
        0,
        REMOTE_FILE_SIZE,
        nullptr,
        false /* setToZeros */);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        writeFileName, readData->Buffer.get(), readData->BufferSize, 0 /* offset */, REMOTE_FILE_SIZE, sparseAttrs.version);
    ASSERT_EQ(nBytesRead, REMOTE_FILE_SIZE);

    // Verify first half
    ASSERT_TRUE(FileDataEquals(
        std::make_shared<FileData>(0, writeData->BufferSize, writeData->Buffer.get(), false),
        std::make_shared<FileData>(0, writeData->BufferSize, readData->Buffer.get(), false)));

    // Verify hole is zeros
    for (size_t idx = writeData->BufferSize; idx < writeData->BufferSize + holeSize; idx++)
    {
        ASSERT_EQ(readData->Buffer.get()[idx], 0);
    }

    // Verify after hole
    ASSERT_TRUE(FileDataEquals(
        std::make_shared<FileData>(
            0, writeData->BufferSize / 2, writeData->Buffer.get() + writeData->BufferSize / 2, false),
        std::make_shared<FileData>(
            0, writeData->BufferSize / 2, readData->Buffer.get() + writeData->BufferSize + holeSize, false)));
    m_spStreamingClient->Close(writeFileName, "" /* version */, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, WriteFileLastAppendWithinChunkBoundary)
{
    // Regression test: UploadCache file size was only updated in Put() when creating
    // a new chunk. Writes extending an existing chunk did not update the tracked size,
    // causing Close() to upload a truncated file to blob.
    //
    // To trigger: choose appendSize and fileSize so that the LAST write lands entirely
    // within an existing chunk (doesn't cross a chunk boundary).
    //
    // With chunkSize=4MB and appendSize=300KB:
    //   - Write at offset ~3.9MB crosses into chunk 1, creating it via Put()
    //   - Subsequent writes at ~4.2MB and ~4.5MB land within chunk 1 (existing)
    //   - Without the fix, UploadCache tracks ~4.2MB instead of ~4.6MB
    //   - Close() uploads truncated data → GetProperties returns wrong size,
    //     ReadFile returns truncated/wrong data
    if (!StorageInterceptConfig::GetInstance().IsWriteSupportEnabled())
    {
        GTEST_SKIP();
    }

    // 4MB + 512KB: enough for writes to continue past the chunk 0→1 boundary crossing
    const size_t fileSize = DEFAULT_STREAMING_CHUNK_SIZE + 512 * 1024;
    // 300KB: does not divide evenly into 4MB, so the boundary-crossing write
    // leaves room for subsequent writes within the new chunk
    const size_t appendSize = 300 * 1024;

    auto writeData = GetRandDataBuffer(fileSize);
    std::string writeFileName = "unalignedAppendsFileSizeTest" + GenerateRandomString(4);

    // WriteAppendsTest writes in append-sized chunks, closes, verifies file size
    // via GetProperties, and reads back the full file for data integrity check.
    // Without the fix, the GetProperties size assertion or data integrity check fails.
    WriteAppendsTest(writeFileName, writeData, appendSize);
}
//---------------------------------------------------------------------------------------------------------------------
// Versioned cache key tests - verify version flows through StreamingClient to cache client calls
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, WriteReadEndToEndVersionedKeyTest)
{
    // End-to-end test: Write → Close (generates version) → GetProperties (gets version)
    // → ReadFile with version → verify data integrity AND verify the generated version
    // was used in cache Upload/Download calls.
    OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnable, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnableRemoteAsync, "false");
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheEnable, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableVersionedCacheKeys, "true");

    auto mockCache = std::make_shared<InMemoryCacheClient>();
    auto attrCache = std::make_shared<InMemoryAttributesCache>();
    auto client = std::make_shared<StreamingClient>("", mockCache, attrCache);

    std::string writeFileName = "e2eVersionTest" + GenerateRandomString(4);
    auto writeData = GetRandDataBuffer(REMOTE_FILE_SIZE);

    // Write the file
    ssize_t nBytesWritten = client->WriteFile(
        writeFileName, writeData->Buffer.get(), writeData->BufferSize, 0);
    ASSERT_EQ(nBytesWritten, writeData->BufferSize);

    // Close → generates version UUID, uploads chunks to cache with versioned keys
    client->Close(writeFileName, "" /* version */, true /* isWriteMode */);

    // Verify version was generated and stored in attr cache
    FileAttributes attrs;
    int nResult = client->GetProperties(writeFileName, attrs);
    ASSERT_EQ(nResult, 0);
    ASSERT_FALSE(attrs.version.empty());
    ASSERT_EQ(attrs.size, writeData->BufferSize);
    const std::string generatedVersion = attrs.version;

    // Verify all cache Upload calls during Close used the generated version
    ASSERT_GT(mockCache->m_uploadCalls.size(), 0u);
    for (const auto& uploadCall : mockCache->m_uploadCalls)
    {
        ASSERT_EQ(uploadCall.version, generatedVersion);
        ASSERT_TRUE(uploadCall.isWrite);
    }

    // Read back the file using the generated version
    std::shared_ptr<FileData> readData = std::make_shared<FileData>(
        0, writeData->BufferSize, nullptr, false);
    ssize_t nBytesRead = client->ReadFile(
        writeFileName, readData->Buffer.get(), readData->BufferSize,
        0, writeData->BufferSize, generatedVersion);
    ASSERT_EQ(nBytesRead, writeData->BufferSize);

    // Verify data integrity
    ASSERT_TRUE(FileDataEquals(writeData, readData));

    // Verify read DownloadSync calls also used the generated version
    bool foundVersionedDownload = false;
    for (const auto& downloadCall : mockCache->m_downloadCalls)
    {
        if (downloadCall.version == generatedVersion)
        {
            foundVersionedDownload = true;
            break;
        }
    }
    ASSERT_TRUE(foundVersionedDownload);

    client->Close(writeFileName, "" /* version */, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, WriteReadWithVersionedKeysDisabledTest)
{
    // Verify that when enableVersionedCacheKeys is false, write-close does NOT generate a version
    // and cache upload/download calls use empty version (pre-PR2 behavior).
    OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnable, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnableRemoteAsync, "false");
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheEnable, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableVersionedCacheKeys, "false");

    auto mockCache = std::make_shared<InMemoryCacheClient>();
    auto attrCache = std::make_shared<InMemoryAttributesCache>();
    auto client = std::make_shared<StreamingClient>("", mockCache, attrCache);

    std::string writeFileName = "noVersionTest" + GenerateRandomString(4);
    auto writeData = GetRandDataBuffer(REMOTE_FILE_SIZE);

    ssize_t nBytesWritten = client->WriteFile(
        writeFileName, writeData->Buffer.get(), writeData->BufferSize, 0);
    ASSERT_EQ(nBytesWritten, writeData->BufferSize);

    client->Close(writeFileName, "" /* version */, true /* isWriteMode */);

    // Version should be empty — no UUID generated when flag is off
    FileAttributes attrs;
    int nResult = client->GetProperties(writeFileName, attrs);
    ASSERT_EQ(nResult, 0);
    ASSERT_TRUE(attrs.version.empty());

    // All cache Upload calls should have empty version
    ASSERT_GT(mockCache->m_uploadCalls.size(), 0u);
    for (const auto& uploadCall : mockCache->m_uploadCalls)
    {
        ASSERT_TRUE(uploadCall.version.empty());
    }

    client->Close(writeFileName, "" /* version */, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, DifferentVersionsCacheSeparatelyTest)
{
    // Verify that reading the same file with different versions results in separate cache entries.
    // With different versions, the cache key differs, so v2 misses even after v1 populated the cache.
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheEnable, "true");

    auto mockCache = std::make_shared<InMemoryCacheClient>();
    auto client = std::make_shared<StreamingClient>(
        "", mockCache, std::make_shared<InMemoryAttributesCache>());

    client->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);

    // Read with version v1 — cold miss, fetches from blob, uploads to cache
    std::vector<char> readBuf(DEFAULT_STREAMING_CHUNK_SIZE);
    client->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 0, REMOTE_FILE_SIZE, "version-v1");
    client->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);

    size_t uploadsAfterV1 = mockCache->m_uploadCalls.size();
    ASSERT_GT(uploadsAfterV1, 0u);

    // Verify version-v1 was passed through to Upload calls
    for (size_t i = 0; i < uploadsAfterV1; i++)
    {
        ASSERT_EQ(mockCache->m_uploadCalls[i].version, "version-v1");
    }

    // Read with version v2 — should miss cache again (different versioned key)
    client->ReadFile(
        m_strRemoteFilePath, readBuf.data(), readBuf.size(), 0, REMOTE_FILE_SIZE, "version-v2");

    // Should have new upload calls for v2 (cache miss → fetch from blob → upload)
    ASSERT_GT(mockCache->m_uploadCalls.size(), uploadsAfterV1);

    // Verify version-v2 was passed through to the new Upload calls
    for (size_t i = uploadsAfterV1; i < mockCache->m_uploadCalls.size(); i++)
    {
        ASSERT_EQ(mockCache->m_uploadCalls[i].version, "version-v2");
    }

    // Verify DownloadSync calls also received the correct versions
    bool foundV1Download = false, foundV2Download = false;
    for (const auto& call : mockCache->m_downloadCalls)
    {
        if (call.version == "version-v1") foundV1Download = true;
        if (call.version == "version-v2") foundV2Download = true;
    }
    ASSERT_TRUE(foundV1Download);
    ASSERT_TRUE(foundV2Download);

    client->Close(m_strRemoteFilePath, m_strFileVersion, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, GetPropertiesReturnsVersionFromAttrCacheTest)
{
    // Verify that GetProperties returns version from attributes cache (populated by listdir or prior open).
    // This tests the attr cache → fd.Attributes.version → ReadFile flow.
    FileAttributes fileAttr;
    int nResult = m_spStreamingClient->GetProperties(m_strRemoteFilePath, fileAttr);
    ASSERT_EQ(nResult, 0);
    ASSERT_EQ(fileAttr.isDir, false);

    // For blob-backed files, version should be non-empty (dacsfileversion or etag fallback)
    ASSERT_FALSE(fileAttr.version.empty());

    // A second GetProperties should hit the attributes cache and return the same version
    FileAttributes fileAttr2;
    nResult = m_spStreamingClient->GetProperties(m_strRemoteFilePath, fileAttr2);
    ASSERT_EQ(nResult, 0);
    ASSERT_EQ(fileAttr.version, fileAttr2.version);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, EvictCacheForFileVersionOnWriteCloseTest)
{
    // In consistent mode, Close() with a non-empty version (old version from open) should call
    // DeleteGroup to evict the old version's cache chunks.
    OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnable, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnableRemoteAsync, "false");
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheEnable, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableVersionedCacheKeys, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableConsistentMode, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableGarbageCollectionInConsistentMode, "true");

    auto mockCache = std::make_shared<InMemoryCacheClient>();
    auto client = std::make_shared<StreamingClient>("", mockCache, nullptr /* no attr cache in consistent mode */);

    std::string writeFileName = "evictTest" + GenerateRandomString(4);
    auto writeData = GetRandDataBuffer(REMOTE_FILE_SIZE);

    // Write the file
    ssize_t nBytesWritten = client->WriteFile(
        writeFileName, writeData->Buffer.get(), writeData->BufferSize, 0);
    ASSERT_EQ(nBytesWritten, writeData->BufferSize);

    // Close with a fake old version — simulates overwrite where Open() found an existing file
    const std::string oldVersion = "old-version-etag-12345";
    client->Close(writeFileName, oldVersion, true /* isWriteMode */);

    // Verify DeleteGroup was called with a groupId derived from the old version
    ASSERT_GT(mockCache->m_deleteGroupCalls.size(), 0u);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, EvictCacheForFileVersionSkippedOnEmptyVersionTest)
{
    // When version is empty (new file, no old version), DeleteGroup should NOT be called.
    OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnable, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameWriteEnableRemoteAsync, "false");
    OverrideConfig(StorageInterceptConstants::ConfigNameCacheEnable, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableVersionedCacheKeys, "true");
    OverrideConfig(StorageInterceptConstants::ConfigNameEnableConsistentMode, "true");

    auto mockCache = std::make_shared<InMemoryCacheClient>();
    auto client = std::make_shared<StreamingClient>("", mockCache, nullptr);

    std::string writeFileName = "noEvictTest" + GenerateRandomString(4);
    auto writeData = GetRandDataBuffer(REMOTE_FILE_SIZE);

    ssize_t nBytesWritten = client->WriteFile(
        writeFileName, writeData->Buffer.get(), writeData->BufferSize, 0);
    ASSERT_EQ(nBytesWritten, writeData->BufferSize);

    // Close with empty version — new file, no old version to evict
    client->Close(writeFileName, "" /* empty version */, true /* isWriteMode */);

    // DeleteGroup should NOT have been called
    ASSERT_EQ(mockCache->m_deleteGroupCalls.size(), 0u);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, OverwriteFileTest)
{
    // Write V1 → Close → Write V2 (overwrite with old version) → Close → Read back V2.
    // Tests the full overwrite flow using m_spStreamingClient against actual backend.
    if (!StorageInterceptConfig::GetInstance().IsWriteSupportEnabled())
    {
        GTEST_SKIP();
    }

    std::string writeFileName = "overwriteTest" + GenerateRandomString(4);

    // Write V1
    auto v1Data = GetRandDataBuffer(REMOTE_FILE_SIZE);
    ssize_t nBytesWritten = m_spStreamingClient->WriteFile(
        writeFileName, v1Data->Buffer.get(), v1Data->BufferSize, 0);
    ASSERT_EQ(nBytesWritten, v1Data->BufferSize);
    m_spStreamingClient->Close(writeFileName, "" /* version */, true /* isWriteMode */);

    // Get V1's version
    FileAttributes v1Attrs;
    m_spStreamingClient->GetProperties(writeFileName, v1Attrs);
    ASSERT_FALSE(v1Attrs.version.empty());
    const std::string v1Version = v1Attrs.version;

    // Write V2 — pass V1's version as the old version (simulates overwrite)
    auto v2Data = GetRandDataBuffer(REMOTE_FILE_SIZE);
    nBytesWritten = m_spStreamingClient->WriteFile(
        writeFileName, v2Data->Buffer.get(), v2Data->BufferSize, 0);
    ASSERT_EQ(nBytesWritten, v2Data->BufferSize);
    m_spStreamingClient->Close(writeFileName, v1Version, true /* isWriteMode */);

    // Get V2's version — should be different from V1
    FileAttributes v2Attrs;
    m_spStreamingClient->GetProperties(writeFileName, v2Attrs);
    ASSERT_FALSE(v2Attrs.version.empty());
    ASSERT_NE(v1Version, v2Attrs.version);

    // Read back — should get V2 data, not V1
    std::shared_ptr<FileData> readData = std::make_shared<FileData>(
        0, v2Data->BufferSize, nullptr, false);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        writeFileName, readData->Buffer.get(), readData->BufferSize,
        0, v2Data->BufferSize, v2Attrs.version);
    ASSERT_EQ(nBytesRead, v2Data->BufferSize);
    ASSERT_TRUE(FileDataEquals(v2Data, readData));

    m_spStreamingClient->Close(writeFileName, "" /* version */, false /* isWriteMode */);
}
//---------------------------------------------------------------------------------------------------------------------