//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// Unit tests for SocketConnection::Recvfile (splice zero-copy path) and
// IBConnection::Recvfile / Read (IB buffered fallback path).
//---------------------------------------------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "SocketConnection.h"
#include "IBConnection.h"
#include "IBConstants.h"
#include "Logger.h"

#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace ibutils;

// Helper: create a dummy ibv_qp shared_ptr for IBConnection tests.
// IBConnection::GetId() dereferences m_qp->qp_num, so we need a real struct.
static shared_ibv_qp MakeDummyQp()
{
    // Allocate a zero-initialized ibv_qp on the heap with a custom deleter
    // that simply frees without calling ibv_destroy_qp (no real IB hardware).
    auto* qp = new ibv_qp{};
    qp->qp_num = 99999; // dummy QP number for log messages
    return shared_ibv_qp(qp, [](ibv_qp* p) { delete p; });
}
//---------------------------------------------------------------------------------------------------------------------
// Helper: create a temp file, return its fd. Caller owns the fd.
static int CreateTempFile(const std::string& path)
{
    int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    EXPECT_GE(fd, 0) << "Failed to create temp file: " << path;
    return fd;
}

// Helper: read entire contents of a file into a vector
static std::vector<char> ReadFileContents(const std::string& path)
{
    int fd = open(path.c_str(), O_RDONLY);
    EXPECT_GE(fd, 0);
    struct stat st{};
    fstat(fd, &st);
    std::vector<char> data(st.st_size);
    size_t totalRead = 0;
    while (totalRead < data.size())
    {
        ssize_t n = read(fd, data.data() + totalRead, data.size() - totalRead);
        EXPECT_GT(n, 0);
        totalRead += n;
    }
    close(fd);
    return data;
}

//=============================================================================
// SocketConnection::Recvfile tests (splice path)
//=============================================================================
class SocketConnectionRecvfileTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create a Unix socketpair — splice() works with AF_UNIX sockets
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, m_sockpair), 0);
        m_tempPath = "/tmp/recvfile_test_" + std::to_string(getpid());
    }

    void TearDown() override
    {
        close(m_sockpair[0]);
        close(m_sockpair[1]);
        unlink(m_tempPath.c_str());
    }

    // m_sockpair[0] = "remote" end (we write data here)
    // m_sockpair[1] = "local" end (SocketConnection reads from here)
    int m_sockpair[2]{};
    std::string m_tempPath;
};
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SocketConnectionRecvfileTest, HappyPath_SmallData)
{
    const std::string payload = "Hello, splice zero-copy!";
    const size_t payloadSize = payload.size();

    // Writer thread: send data then close
    std::thread writer([&]()
    {
        ssize_t n = write(m_sockpair[0], payload.c_str(), payloadSize);
        EXPECT_EQ(n, static_cast<ssize_t>(payloadSize));
        close(m_sockpair[0]);
        m_sockpair[0] = -1;
    });

    int fileFd = CreateTempFile(m_tempPath);
    SocketConnection conn(m_sockpair[1]);
    bool result = conn.Recvfile(fileFd, payloadSize);
    close(fileFd);
    writer.join();

    EXPECT_TRUE(result);

    auto contents = ReadFileContents(m_tempPath);
    ASSERT_EQ(contents.size(), payloadSize);
    EXPECT_EQ(std::string(contents.data(), contents.size()), payload);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SocketConnectionRecvfileTest, HappyPath_LargeData)
{
    // 8 MiB — larger than the default pipe buffer (64 KiB) and our 4 MiB pipe resize
    const size_t payloadSize = 8 * 1024 * 1024;
    std::vector<char> payload(payloadSize);
    for (size_t i = 0; i < payloadSize; i++)
    {
        payload[i] = static_cast<char>(i & 0xFF);
    }

    std::thread writer([&]()
    {
        size_t totalWritten = 0;
        while (totalWritten < payloadSize)
        {
            ssize_t n = write(m_sockpair[0], payload.data() + totalWritten,
                              payloadSize - totalWritten);
            EXPECT_GT(n, 0);
            if (n <= 0) break;
            totalWritten += n;
        }
        close(m_sockpair[0]);
        m_sockpair[0] = -1;
    });

    int fileFd = CreateTempFile(m_tempPath);
    SocketConnection conn(m_sockpair[1]);
    bool result = conn.Recvfile(fileFd, payloadSize);
    close(fileFd);
    writer.join();

    EXPECT_TRUE(result);

    auto contents = ReadFileContents(m_tempPath);
    ASSERT_EQ(contents.size(), payloadSize);
    EXPECT_EQ(contents, payload);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SocketConnectionRecvfileTest, UnexpectedEOF_PeerClosesEarly)
{
    const size_t expectedSize = 1024;
    const std::string partialPayload = "short";

    // Write less data than expected, then close
    ssize_t n = write(m_sockpair[0], partialPayload.c_str(), partialPayload.size());
    EXPECT_EQ(n, static_cast<ssize_t>(partialPayload.size()));
    close(m_sockpair[0]);
    m_sockpair[0] = -1;

    int fileFd = CreateTempFile(m_tempPath);
    SocketConnection conn(m_sockpair[1]);
    bool result = conn.Recvfile(fileFd, expectedSize);
    close(fileFd);

    // Should fail because peer sent less data than totalLength
    EXPECT_FALSE(result);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(SocketConnectionRecvfileTest, ZeroLength)
{
    int fileFd = CreateTempFile(m_tempPath);
    SocketConnection conn(m_sockpair[1]);
    bool result = conn.Recvfile(fileFd, 0);
    close(fileFd);

    // Zero-length transfer should succeed immediately
    EXPECT_TRUE(result);
}


//=============================================================================
// IBConnection::Recvfile tests (buffered fallback path)
//=============================================================================
class IBConnectionRecvfileTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_tempPath = "/tmp/ib_recvfile_test_" + std::to_string(getpid());
        m_buffer.resize(IB_BUFFER_SIZE, 0);
    }

    void TearDown() override
    {
        unlink(m_tempPath.c_str());
    }

    std::string m_tempPath;
    std::vector<char> m_buffer;
};
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionRecvfileTest, HappyPath_WriteToFile)
{
    const size_t dataSize = 4096;
    // Fill buffer with known pattern
    for (size_t i = 0; i < dataSize; i++)
    {
        m_buffer[i] = static_cast<char>(i & 0xFF);
    }

    // IBConnection needs a real ibv_qp for GetId(), but we only test Recvfile
    // which doesn't use the QP. Pass nullptr and avoid calling GetId().
    // We construct with a raw pointer to the buffer and bytesReceived = dataSize.
    // Unfortunately IBConnection takes shared_ibv_qp (shared_ptr with custom deleter).
    // We'll create a dummy one.
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, static_cast<int>(dataSize));

    int fileFd = CreateTempFile(m_tempPath);
    bool result = conn.Recvfile(fileFd, dataSize);
    close(fileFd);

    EXPECT_TRUE(result);

    auto contents = ReadFileContents(m_tempPath);
    ASSERT_EQ(contents.size(), dataSize);
    for (size_t i = 0; i < dataSize; i++)
    {
        EXPECT_EQ(contents[i], static_cast<char>(i & 0xFF)) << "Mismatch at byte " << i;
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionRecvfileTest, HappyPath_LargeTransfer)
{
    // 8 MiB — tests the loop (WRITE_BUF_SIZE is 4 MiB)
    const size_t dataSize = 8 * 1024 * 1024;
    m_buffer.resize(dataSize);
    for (size_t i = 0; i < dataSize; i++)
    {
        m_buffer[i] = static_cast<char>((i * 7) & 0xFF);
    }

    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, static_cast<int>(dataSize));

    int fileFd = CreateTempFile(m_tempPath);
    bool result = conn.Recvfile(fileFd, dataSize);
    close(fileFd);

    EXPECT_TRUE(result);

    auto contents = ReadFileContents(m_tempPath);
    ASSERT_EQ(contents.size(), dataSize);
    EXPECT_EQ(contents, std::vector<char>(m_buffer.begin(), m_buffer.begin() + dataSize));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionRecvfileTest, FailsWhenBufferTooSmall)
{
    // Buffer has 100 bytes, but we request 200
    const int bufferDataSize = 100;
    for (int i = 0; i < bufferDataSize; i++)
    {
        m_buffer[i] = static_cast<char>(i);
    }

    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, bufferDataSize);

    int fileFd = CreateTempFile(m_tempPath);
    bool result = conn.Recvfile(fileFd, 200); // request more than available
    close(fileFd);

    EXPECT_FALSE(result);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionRecvfileTest, ZeroLength)
{
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 100);

    int fileFd = CreateTempFile(m_tempPath);
    bool result = conn.Recvfile(fileFd, 0);
    close(fileFd);

    EXPECT_TRUE(result);
}

//=============================================================================
// IBConnection::Read boundary tests
//=============================================================================
class IBConnectionReadTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_buffer.resize(IB_BUFFER_SIZE, 0);
    }

    std::vector<char> m_buffer;
};
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionReadTest, ReadExactBuffer)
{
    const int dataSize = 256;
    for (int i = 0; i < dataSize; i++)
    {
        m_buffer[i] = static_cast<char>(i);
    }

    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, dataSize);

    char out[256];
    int n = conn.Read(out, dataSize);
    EXPECT_EQ(n, dataSize);
    for (int i = 0; i < dataSize; i++)
    {
        EXPECT_EQ(out[i], static_cast<char>(i));
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionReadTest, ReadBeyondBuffer_ReturnsZero)
{
    const int dataSize = 10;
    for (int i = 0; i < dataSize; i++)
    {
        m_buffer[i] = static_cast<char>(i);
    }

    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, dataSize);

    // Read all data first
    char out[64];
    int n = conn.Read(out, dataSize);
    EXPECT_EQ(n, dataSize);

    // Now read again — should return 0 (not a wrapped-around huge value)
    n = conn.Read(out, 10);
    EXPECT_EQ(n, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionReadTest, PartialReads)
{
    const int dataSize = 100;
    for (int i = 0; i < dataSize; i++)
    {
        m_buffer[i] = static_cast<char>(i);
    }

    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, dataSize);

    // Read in chunks of 30
    char out[100];
    int total = 0;
    int n;

    n = conn.Read(out + total, 30);
    EXPECT_EQ(n, 30);
    total += n;

    n = conn.Read(out + total, 30);
    EXPECT_EQ(n, 30);
    total += n;

    n = conn.Read(out + total, 30);
    EXPECT_EQ(n, 30);
    total += n;

    // Only 10 bytes left
    n = conn.Read(out + total, 30);
    EXPECT_EQ(n, 10);
    total += n;

    EXPECT_EQ(total, dataSize);
    for (int i = 0; i < dataSize; i++)
    {
        EXPECT_EQ(out[i], static_cast<char>(i));
    }

    // Should return 0 now
    n = conn.Read(out, 1);
    EXPECT_EQ(n, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(IBConnectionReadTest, ZeroBytesReceived_ReadReturnsZero)
{
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    char out[16];
    int n = conn.Read(out, 16);
    EXPECT_EQ(n, 0);
}
//---------------------------------------------------------------------------------------------------------------------
