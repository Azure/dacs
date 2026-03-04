//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StreamingClientTest.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptConfig.h"
#include "StreamingClient.h"
#include "InMemoryAttributesCache.h"
#include "RemoteCacheClient.h"
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
    m_spStreamingClient->Close(m_strRemoteFilePath, false /* isWriteMode */);
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
    m_spStreamingClient->Close(writeFileName, true /* isWriteMode */);

    // Read back the file to verify data integrity
    std::shared_ptr<FileData> readData = std::make_shared<FileData>(
        0,
        writeData->BufferSize,
        nullptr,
        false /* setToZeros */);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        writeFileName,
        readData->Buffer.get(),
        readData->BufferSize,
        0 /* offset */,
        REMOTE_FILE_SIZE);
    ASSERT_EQ(nBytesRead, writeData->BufferSize);
    ASSERT_TRUE(FileDataEquals(writeData, readData));

    // Close the file
    m_spStreamingClient->Close(writeFileName, false /* isWriteMode */);
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
    m_spStreamingClient->Close(remotePath, true /* isWriteMode */);

    // Get properties to verify file size updated
    FileAttributes attributes;
    int nResult = m_spStreamingClient->GetProperties(
        remotePath,
        attributes /* [out] attributes */);
    ASSERT_EQ(nResult, 0);
    ASSERT_EQ(attributes.size, writeData->BufferSize);

    // Read back the file to verify data integrity
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
        writeData->BufferSize);
    ASSERT_EQ(nBytesRead, writeData->BufferSize);
    ASSERT_TRUE(FileDataEquals(writeData, readData));
    m_spStreamingClient->Close(remotePath, false /* isWriteMode */);
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
    m_spStreamingClient->Close(writeFileName, true /* isWriteMode */);

    // Read back the file to verify data integrity
    std::shared_ptr<FileData> readData = std::make_shared<FileData>(
        0,
        REMOTE_FILE_SIZE,
        nullptr,
        false /* setToZeros */);
    ssize_t nBytesRead = m_spStreamingClient->ReadFile(
        writeFileName,
        readData->Buffer.get(),
        readData->BufferSize,
        0 /* offset */,
        REMOTE_FILE_SIZE);
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
    m_spStreamingClient->Close(writeFileName, false /* isWriteMode */);
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