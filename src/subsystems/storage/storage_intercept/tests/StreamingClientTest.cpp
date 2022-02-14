//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StreamingClientTest.h"
#include "StreamingClient.h"
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
TEST_F(StreamingClientTest, ReadFilePartialTest)
{
    std::vector<char> readBuf(REMOTE_FILE_SIZE / 2);

    ssize_t nBytesRead = StreamingClient::GetInstance().ReadFile(
        m_strRemoteFilePath,
        readBuf.data(),
        readBuf.size(),
        10 /* offset */
    );
    ASSERT_EQ(nBytesRead, REMOTE_FILE_SIZE / 2);
    // Data integrity check
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, 10, 0, nBytesRead)));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadChunkFullFileTest)
{
    std::vector<char> readBuf(REMOTE_FILE_SIZE);
    ssize_t nBytesRead = StreamingClient::GetInstance().ReadFile(
        m_strRemoteFilePath,
        readBuf.data(),
        readBuf.size(),
        0 /* offset */
    );
    ASSERT_EQ(nBytesRead, REMOTE_FILE_SIZE);
    /* Data integrity check */
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEquals(m_vecBuffer, readBuf)));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadFileTinyReadsTest)
{
    std::vector<char> readBuf(50);

    // Read first 50 bytes.
    ssize_t nBytesRead = StreamingClient::GetInstance().ReadFile(
        m_strRemoteFilePath,
        readBuf.data(),
        readBuf.size(),
        0 /* offset */
    );
    ASSERT_EQ(nBytesRead, 50);
    // Data integrity check
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, 0, 0, 50)));

    // Read next 50 bytes.
    nBytesRead = StreamingClient::GetInstance().ReadFile(
        m_strRemoteFilePath,
        readBuf.data(),
        readBuf.size(),
        50 /* offset */
    );
    ASSERT_EQ(nBytesRead, 50);
    // Data integrity check
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, 50, 0, 50)));

    // Read next 50 bytes.
    nBytesRead = StreamingClient::GetInstance().ReadFile(
        m_strRemoteFilePath,
        readBuf.data(),
        readBuf.size(),
        100 /* offset */
    );
    ASSERT_EQ(nBytesRead, 50);
    // Data integrity check
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, 100, 0, 50)));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, ReadSizeLargerThanFileSizeTest)
{
    std::vector<char> readBuf(2 * REMOTE_FILE_SIZE);

    ssize_t nBytesRead = StreamingClient::GetInstance().ReadFile(
        m_strRemoteFilePath,
        readBuf.data(),
        readBuf.size(),
        0 /* offset */
    );
    ASSERT_EQ(nBytesRead, REMOTE_FILE_SIZE);
    // Data integrity check
    ASSERT_IF(m_bUsingMock, ASSERT_TRUE(BufferEqualsInRange(m_vecBuffer, readBuf, 0, 0, REMOTE_FILE_SIZE)));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StreamingClientTest, RootDirPathTest)
{
    const std::string rootDirPath = "";
    std::string continuationToken;
    std::vector<DirectoryEntry> dirEntries;

    /* Tests if ReadDirectory call on root path works correctly */
    continuationToken = StreamingClient::GetInstance().ReadDirectory(
        rootDirPath,
        continuationToken,
        dirEntries /* [out] dirents */
    );
    ASSERT_TRUE(dirEntries.size() > 0);

    /* Tests if GetProperties call on root path, identifies it as a valid directory */
    FileAttributes attr;
    int nResult = StreamingClient::GetInstance().GetProperties(
        rootDirPath,
        attr
    );
    ASSERT_EQ(nResult, 0);
    ASSERT_EQ(attr.isDir, true);
}
//---------------------------------------------------------------------------------------------------------------------