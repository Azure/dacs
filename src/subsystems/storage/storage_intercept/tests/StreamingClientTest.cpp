//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StreamingClientTest.h"
#include "StorageInterceptConstants.h"
#include "StreamingClient.h"
#include "InMemoryAttributesCache.h"
#include <atomic>
#include <vector>
#include <mutex>
#include <thread>
//---------------------------------------------------------------------------------------------------------------------
#define ASSERT_IF(condition, stmt)                                                         { if (condition) { stmt; } }
//---------------------------------------------------------------------------------------------------------------------
StreamingClientTest::StreamingClientTest()
    : TestBase(), m_vecBuffer(m_vecFileBuf)
{
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClientTest::SetUp()
{
    m_spStreamingClient = std::make_shared<StreamingClient>("", nullptr, std::make_shared<InMemoryAttributesCache>());
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
        REMOTE_FILE_SIZE);
    ASSERT_EQ(nBytesRead, expectedBytesRead);
    // Data integrity check
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, offset, 0, nBytesRead)));

    ASSERT_EQ(m_spStreamingClient->GetReadChunksZeroCopy(), expectedZeroCopyReads);
    // Close the file, forcing to purge the cache
    m_spStreamingClient->Close(m_strRemoteFilePath);
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