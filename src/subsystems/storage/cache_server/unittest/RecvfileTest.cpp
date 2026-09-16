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

// Fake ibverbs ops for the dummy QP — avoids null-pointer dereference when
// IBConnection::FinalizeWrite() calls ibv_post_send (which is a static-inline
// function in verbs.h that dispatches through qp->context->ops.post_send).
namespace fake_ib_server {

static int fake_post_send(struct ibv_qp*, struct ibv_send_wr*, struct ibv_send_wr**)
{
    return 0; // success — no real IB hardware
}

static int fake_post_recv(struct ibv_qp*, struct ibv_recv_wr*, struct ibv_recv_wr**)
{
    return 0;
}

static struct ibv_context fake_context{};

void InitContext()
{
    memset(&fake_context, 0, sizeof(fake_context));
    fake_context.ops.post_send = fake_post_send;
    fake_context.ops.post_recv = fake_post_recv;
}

} // namespace fake_ib_server

// Helper: create a dummy ibv_qp shared_ptr for IBConnection tests.
// IBConnection::GetId() dereferences m_qp->qp_num, so we need a real struct.
// The QP also needs a valid context with ops table for FinalizeWrite() (Sendfile tests).
static shared_ibv_qp MakeDummyQp()
{
    static bool initialized = false;
    if (!initialized)
    {
        fake_ib_server::InitContext();
        initialized = true;
    }

    auto* qp = new ibv_qp{};
    qp->qp_num = 99999; // dummy QP number for log messages
    qp->context = &fake_ib_server::fake_context; // ops table for inline ibv_post_send dispatch
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
    EXPECT_GE(st.st_size, 0);
    std::vector<char> data(static_cast<size_t>(st.st_size));
    size_t totalRead = 0;
    while (totalRead < data.size())
    {
        ssize_t n = read(fd, data.data() + totalRead, data.size() - totalRead);
        EXPECT_GT(n, 0);
        totalRead += static_cast<size_t>(n);
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
// Verify: splice zero-copy receives small payload from socket and writes correct bytes to file.
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
// Verify: splice handles 8 MiB transfer (larger than pipe buffer) with correct byte ordering.
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
            totalWritten += static_cast<size_t>(n);
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
// Verify: peer closes socket before sending expected bytes → Recvfile returns false.
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
// Verify: zero-length transfer is a valid no-op (returns true immediately).
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
// Verify: IB Recvfile writes received buffer data to file with correct byte pattern.
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
// Verify: IB Recvfile handles 8 MiB transfer (exercises the 4 MB chunked write loop).
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
// Verify: requesting more bytes than available in IB buffer → returns false (no buffer overread).
TEST_F(IBConnectionRecvfileTest, FailsWhenBufferTooSmall)
{
    // Buffer has 100 bytes, but we request 200
    const int bufferDataSize = 100;
    for (int i = 0; i < bufferDataSize; i++)
    {
        m_buffer[static_cast<size_t>(i)] = static_cast<char>(i);
    }

    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, bufferDataSize);

    int fileFd = CreateTempFile(m_tempPath);
    bool result = conn.Recvfile(fileFd, 200); // request more than available
    close(fileFd);

    EXPECT_FALSE(result);
}
//---------------------------------------------------------------------------------------------------------------------
// Verify: zero-length IB Recvfile is a valid no-op.
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
// Verify: Read() returns exactly dataSize bytes with correct content.
TEST_F(IBConnectionReadTest, ReadExactBuffer)
{
    const int dataSize = 256;
    for (int i = 0; i < dataSize; i++)
    {
        m_buffer[static_cast<size_t>(i)] = static_cast<char>(i);
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
// Verify: reading past end of received data returns 0 (not a wrapped/negative value).
TEST_F(IBConnectionReadTest, ReadBeyondBuffer_ReturnsZero)
{
    const int dataSize = 10;
    for (int i = 0; i < dataSize; i++)
    {
        m_buffer[static_cast<size_t>(i)] = static_cast<char>(i);
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
// Verify: multiple partial reads drain buffer correctly; final read returns remaining bytes.
TEST_F(IBConnectionReadTest, PartialReads)
{
    const int dataSize = 100;
    for (int i = 0; i < dataSize; i++)
    {
        m_buffer[static_cast<size_t>(i)] = static_cast<char>(i);
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
// Verify: zero bytes received → Read() immediately returns 0.
TEST_F(IBConnectionReadTest, ZeroBytesReceived_ReadReturnsZero)
{
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    char out[16];
    int n = conn.Read(out, 16);
    EXPECT_EQ(n, 0);
}

//=============================================================================
// IBConnection::Write / WriteFinal data integrity tests
//=============================================================================
class IBConnectionWriteTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_buffer.resize(IB_BUFFER_SIZE, 0);
    }

    std::vector<char> m_buffer;
};
//---------------------------------------------------------------------------------------------------------------------
// Verify: Write() places data at correct offset in the shared buffer.
TEST_F(IBConnectionWriteTest, Write_DataIntegrity_BytesAtCorrectOffset)
{
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    const std::string data = "Hello, IB World!";
    int n = conn.Write(const_cast<char*>(data.data()), static_cast<int>(data.size()));
    EXPECT_EQ(static_cast<int>(data.size()), n);

    // Verify data was written to the buffer at offset 0
    EXPECT_EQ(0, memcmp(m_buffer.data(), data.data(), data.size()));
}
//---------------------------------------------------------------------------------------------------------------------
// Verify: multiple Write() calls accumulate data contiguously (no gaps or overwrites).
TEST_F(IBConnectionWriteTest, Write_MultipleWrites_DataAccumulatesCorrectly)
{
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    const std::string part1 = "HEADER:";
    const std::string part2 = "DATA:12345";

    conn.Write(const_cast<char*>(part1.data()), static_cast<int>(part1.size()));
    conn.Write(const_cast<char*>(part2.data()), static_cast<int>(part2.size()));

    std::string expected = part1 + part2;
    EXPECT_EQ(0, memcmp(m_buffer.data(), expected.data(), expected.size()));
}
//---------------------------------------------------------------------------------------------------------------------
// Verify: Write() exceeding IB_BUFFER_SIZE returns -1 (no buffer overflow).
TEST_F(IBConnectionWriteTest, Write_ExceedsBufferSize_ReturnsError)
{
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    // Fill to capacity, then try one more byte
    std::vector<char> fillData(IB_BUFFER_SIZE, 'X');
    int n = conn.Write(fillData.data(), IB_BUFFER_SIZE);
    EXPECT_EQ(IB_BUFFER_SIZE, n);

    char extra = 'Y';
    n = conn.Write(&extra, 1);
    EXPECT_EQ(-1, n);
}
//---------------------------------------------------------------------------------------------------------------------
// Verify: Write() with size=0 is a valid no-op (returns 0).
TEST_F(IBConnectionWriteTest, Write_ZeroBytes_Succeeds)
{
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    char dummy = 'x';
    int n = conn.Write(&dummy, 0);
    EXPECT_EQ(0, n);
}
//---------------------------------------------------------------------------------------------------------------------
// Verify: Write() with negative size returns -1 (invalid input guard).
TEST_F(IBConnectionWriteTest, Write_NegativeSize_ReturnsError)
{
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    char dummy = 'x';
    int n = conn.Write(&dummy, -1);
    EXPECT_EQ(-1, n);
}
//---------------------------------------------------------------------------------------------------------------------
// Verify: server reads request then writes response into same buffer without interference.
TEST_F(IBConnectionWriteTest, ReadAndWrite_UsesSameBuffer_NoOverlap)
{
    // Server uses same buffer for read and write. This test verifies that
    // reading from received data and writing response don't interfere.
    auto dummyQp = MakeDummyQp();

    // Place "request" data in the buffer (simulating what HCA wrote)
    const std::string request = "GET /file.dat";
    memcpy(m_buffer.data(), request.data(), request.size());

    IBConnection conn(dummyQp, m_buffer.data(), 0, static_cast<int>(request.size()));

    // Read the request
    char readBuf[64] = {};
    int nRead = conn.Read(readBuf, sizeof(readBuf));
    EXPECT_EQ(static_cast<int>(request.size()), nRead);
    EXPECT_EQ(0, memcmp(readBuf, request.data(), request.size()));

    // Write a response into the same buffer (server reuses buffer for response)
    const std::string response = "200 OK: file data here";
    int nWrite = conn.Write(const_cast<char*>(response.data()), static_cast<int>(response.size()));
    EXPECT_EQ(static_cast<int>(response.size()), nWrite);

    // The response data should be at the start of the buffer (writeOffset starts at 0)
    EXPECT_EQ(0, memcmp(m_buffer.data(), response.data(), response.size()));
}

//=============================================================================
// IBConnection::Sendfile data integrity tests
//=============================================================================
class IBConnectionSendfileTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_buffer.resize(IB_BUFFER_SIZE, 0);
        m_tempPath = "/tmp/ib_sendfile_test_" + std::to_string(getpid());
    }

    void TearDown() override
    {
        unlink(m_tempPath.c_str());
    }

    // Helper: create a file with known content
    int CreateFileWithContent(const std::vector<char>& content)
    {
        int fd = open(m_tempPath.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        EXPECT_GE(fd, 0);
        size_t written = 0;
        while (written < content.size())
        {
            ssize_t n = write(fd, content.data() + written, content.size() - written);
            EXPECT_GT(n, 0);
            written += static_cast<size_t>(n);
        }
        lseek(fd, 0, SEEK_SET);
        return fd;
    }

    std::vector<char> m_buffer;
    std::string m_tempPath;
};
//---------------------------------------------------------------------------------------------------------------------
// Verify: Sendfile reads file into buffer after header, posts IB send, buffer has correct data.
TEST_F(IBConnectionSendfileTest, HappyPath_SmallFile)
{
    // Create a file with known content
    const size_t fileSize = 1024;
    std::vector<char> fileContent(fileSize);
    for (size_t i = 0; i < fileSize; i++) fileContent[i] = static_cast<char>(i % 251);

    int fd = CreateFileWithContent(fileContent);

    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    // Write a header first (like the real protocol does)
    const std::string header = "response-header";
    conn.Write(const_cast<char*>(header.data()), static_cast<int>(header.size()));

    // Sendfile reads file into buffer after the header, then calls FinalizeWrite
    // which posts ibv_post_send via our fake context ops table.
    bool result = conn.Sendfile(fd, 0, fileSize);
    close(fd);

    EXPECT_TRUE(result);

    // Verify buffer contents: header at offset 0, file data after
    EXPECT_EQ(0, memcmp(m_buffer.data(), header.data(), header.size()));
    EXPECT_EQ(0, memcmp(m_buffer.data() + header.size(), fileContent.data(), fileSize));
}
//---------------------------------------------------------------------------------------------------------------------
// Verify: Sendfile with non-zero file offset reads from correct position.
TEST_F(IBConnectionSendfileTest, Sendfile_WithOffset)
{
    // Create file and read from a non-zero offset
    const size_t fileSize = 4096;
    std::vector<char> fileContent(fileSize);
    for (size_t i = 0; i < fileSize; i++) fileContent[i] = static_cast<char>(i % 127);

    int fd = CreateFileWithContent(fileContent);

    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    const uint64_t offset = 1024;
    const uint64_t length = 2048;
    bool result = conn.Sendfile(fd, offset, length);
    close(fd);

    EXPECT_TRUE(result);

    // Buffer should contain file data starting from offset
    EXPECT_EQ(0, memcmp(m_buffer.data(), fileContent.data() + offset, length));
}
//---------------------------------------------------------------------------------------------------------------------
// Verify: Sendfile rejects request larger than IB_BUFFER_SIZE (no buffer overrun).
TEST_F(IBConnectionSendfileTest, Sendfile_ExceedsBuffer_Fails)
{
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    // Request more than buffer can hold
    bool result = conn.Sendfile(-1, 0, IB_BUFFER_SIZE + 1);
    EXPECT_FALSE(result);
}

//=============================================================================
// End-to-end: Simulated Client→Server data flow
// Tests that data sent by client IBConnection arrives correctly when parsed
// by server IBConnection (using the same wire format).
//=============================================================================
class IBClientServerDataFlowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_serverBuffer.resize(IB_BUFFER_SIZE, 0);
    }

    // Simulate the client's send by capturing SGE data from the test infrastructure.
    // This test doesn't use the client IBConnection directly — it constructs the
    // wire format manually and verifies server-side parsing.

    std::vector<char> m_serverBuffer;
};
//---------------------------------------------------------------------------------------------------------------------
// Verify: server-side IBConnection correctly parses client wire format (prefix+proto+data).
TEST_F(IBClientServerDataFlowTest, UploadProtocol_HeaderPlusData_ParsedCorrectly)
{
    // Build the wire format as the client would send it:
    // [4-byte big-endian length prefix] [protobuf message] [file data]

    // Simulate a protobuf UploadRequest (just bytes for this test)
    const std::string protoMsg = "upload:filename=test.bin,size=4096";
    uint32_t protoSize = static_cast<uint32_t>(protoMsg.size());

    // Big-endian length prefix
    char prefix[4];
    prefix[0] = static_cast<char>((protoSize >> 24) & 0xFFU);
    prefix[1] = static_cast<char>((protoSize >> 16) & 0xFFU);
    prefix[2] = static_cast<char>((protoSize >> 8) & 0xFFU);
    prefix[3] = static_cast<char>(protoSize & 0xFFU);

    // File data (4096 bytes with pattern)
    const size_t fileSize = 4096;
    std::vector<char> fileData(fileSize);
    for (size_t i = 0; i < fileSize; i++) fileData[i] = static_cast<char>((i * 13 + 7) % 256);

    // Assemble into server's receive buffer (simulating contiguous RDMA receive)
    size_t offset = 0;
    memcpy(m_serverBuffer.data() + offset, prefix, 4);
    offset += 4;
    memcpy(m_serverBuffer.data() + offset, protoMsg.data(), protoSize);
    offset += protoSize;
    memcpy(m_serverBuffer.data() + offset, fileData.data(), fileSize);
    offset += fileSize;

    // Create server IBConnection as if data arrived via RDMA
    auto dummyQp = MakeDummyQp();
    IBConnection serverConn(dummyQp, m_serverBuffer.data(), 0, static_cast<int>(offset));

    // --- Server-side parsing (mirrors HandleRequest.cpp) ---

    // Read 4-byte length prefix
    char metaData[4];
    int n = serverConn.Read(metaData, 4);
    EXPECT_EQ(4, n);

    // Parse length (big-endian)
    uint32_t parsedProtoSize =
        (static_cast<uint32_t>(static_cast<uint8_t>(metaData[0])) << 24) |
        (static_cast<uint32_t>(static_cast<uint8_t>(metaData[1])) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(metaData[2])) << 8) |
        static_cast<uint32_t>(static_cast<uint8_t>(metaData[3]));
    EXPECT_EQ(protoSize, parsedProtoSize);

    // Read protobuf message
    std::vector<char> parsedProto(parsedProtoSize);
    n = serverConn.Read(parsedProto.data(), static_cast<int>(parsedProtoSize));
    EXPECT_EQ(static_cast<int>(parsedProtoSize), n);
    EXPECT_EQ(0, memcmp(parsedProto.data(), protoMsg.data(), protoSize));

    // Read file data
    std::vector<char> parsedFileData(fileSize);
    size_t totalRead = 0;
    while (totalRead < fileSize)
    {
        int chunk = serverConn.Read(parsedFileData.data() + totalRead,
                        static_cast<int>(std::min(static_cast<size_t>(1024), fileSize - totalRead)));
        ASSERT_GT(chunk, 0) << "Read failed at offset " << totalRead;
        totalRead += static_cast<size_t>(chunk);
    }
    EXPECT_EQ(fileSize, totalRead);

    // Byte-for-byte verification of file data
    for (size_t i = 0; i < fileSize; i++)
    {
        ASSERT_EQ(fileData[i], parsedFileData[i]) << "File data mismatch at byte " << i;
    }
}
//---------------------------------------------------------------------------------------------------------------------
// Verify: realistic 32 MB chunk (production chunk size) is parsed correctly by server Read().
TEST_F(IBClientServerDataFlowTest, UploadProtocol_LargeChunk_32MB)
{
    // Test with a realistic 32 MB chunk (matches production chunk size)
    const std::string protoMsg(200, 'P');
    uint32_t protoSize = static_cast<uint32_t>(protoMsg.size());
    char prefix[4] = {0, 0, 0, static_cast<char>(protoSize)};

    const size_t chunkSize = 32 * 1024 * 1024;
    std::vector<char> chunkData(chunkSize);
    for (size_t i = 0; i < chunkSize; i++) chunkData[i] = static_cast<char>(i % 251);

    // Assemble into receive buffer
    size_t totalWireSize = 4 + protoSize + chunkSize;
    ASSERT_LE(totalWireSize, static_cast<size_t>(IB_BUFFER_SIZE));

    m_serverBuffer.resize(IB_BUFFER_SIZE);
    memcpy(m_serverBuffer.data(), prefix, 4);
    memcpy(m_serverBuffer.data() + 4, protoMsg.data(), protoSize);
    memcpy(m_serverBuffer.data() + 4 + protoSize, chunkData.data(), chunkSize);

    auto dummyQp = MakeDummyQp();
    IBConnection serverConn(dummyQp, m_serverBuffer.data(), 0, static_cast<int>(totalWireSize));

    // Parse
    char meta[4];
    serverConn.Read(meta, 4);
    uint32_t parsedLen = static_cast<uint8_t>(meta[3]);
    EXPECT_EQ(protoSize, parsedLen);

    std::vector<char> parsedProto(parsedLen);
    serverConn.Read(parsedProto.data(), static_cast<int>(parsedLen));
    EXPECT_EQ(0, memcmp(parsedProto.data(), protoMsg.data(), protoSize));

    // Read chunk data in 4 MB pieces (like the server's buffered read path)
    std::vector<char> parsedChunk(chunkSize);
    size_t totalRead = 0;
    const size_t readChunkSize = 4 * 1024 * 1024;
    while (totalRead < chunkSize)
    {
        size_t toRead = std::min(readChunkSize, chunkSize - totalRead);
        int n = serverConn.Read(parsedChunk.data() + totalRead, static_cast<int>(toRead));
        ASSERT_GT(n, 0) << "Read failed at offset " << totalRead;
        totalRead += static_cast<size_t>(n);
    }
    EXPECT_EQ(chunkSize, totalRead);

    // Verify every byte
    EXPECT_EQ(0, memcmp(parsedChunk.data(), chunkData.data(), chunkSize));
}
//---------------------------------------------------------------------------------------------------------------------
// Verify: server builds download response wire format correctly (prefix+header in buffer).
TEST_F(IBClientServerDataFlowTest, DownloadProtocol_ServerWriteThenSendfile)
{
    // Simulate server building a download response: response header + file data
    auto dummyQp = MakeDummyQp();
    IBConnection serverConn(dummyQp, m_serverBuffer.data(), 0, 0);

    // Server writes response header
    const std::string responseHeader = "response:status=200,size=2048";
    uint32_t respSize = static_cast<uint32_t>(responseHeader.size());
    char prefix[4];
    prefix[0] = static_cast<char>((respSize >> 24) & 0xFFU);
    prefix[1] = static_cast<char>((respSize >> 16) & 0xFFU);
    prefix[2] = static_cast<char>((respSize >> 8) & 0xFFU);
    prefix[3] = static_cast<char>(respSize & 0xFFU);

    serverConn.Write(prefix, 4);
    serverConn.Write(const_cast<char*>(responseHeader.data()), static_cast<int>(responseHeader.size()));

    // Verify the buffer contains the correct wire format
    EXPECT_EQ(0, memcmp(m_serverBuffer.data(), prefix, 4));
    EXPECT_EQ(0, memcmp(m_serverBuffer.data() + 4, responseHeader.data(), responseHeader.size()));
}
//---------------------------------------------------------------------------------------------------------------------
// Verify: server Recvfile writes 1 MB received data to disk; on-disk bytes match original.
TEST_F(IBClientServerDataFlowTest, RecvfileToFile_DataMatchesOriginal)
{
    // Simulate: client sends a 1 MB file → server Recvfile writes to disk → verify on-disk bytes
    const size_t fileSize = 1 * 1024 * 1024;
    std::vector<char> originalData(fileSize);
    for (size_t i = 0; i < fileSize; i++) originalData[i] = static_cast<char>((i * 3 + 17) % 256);

    // Place data after the header in the server buffer (as if client sent header + data)
    size_t headerSize = 100;
    memset(m_serverBuffer.data(), 'H', headerSize); // dummy header
    memcpy(m_serverBuffer.data() + headerSize, originalData.data(), fileSize);

    auto dummyQp = MakeDummyQp();
    IBConnection serverConn(dummyQp, m_serverBuffer.data(), 0, static_cast<int>(headerSize + fileSize));

    // Skip header
    char headerBuf[100];
    int n = serverConn.Read(headerBuf, static_cast<int>(headerSize));
    EXPECT_EQ(static_cast<int>(headerSize), n);

    // Write to file via Recvfile
    std::string tempPath = "/tmp/ib_e2e_recvfile_" + std::to_string(getpid());
    int fileFd = open(tempPath.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fileFd, 0);

    bool result = serverConn.Recvfile(fileFd, fileSize);
    close(fileFd);
    EXPECT_TRUE(result);

    // Read file back and verify
    auto contents = ReadFileContents(tempPath);
    unlink(tempPath.c_str());

    ASSERT_EQ(fileSize, contents.size());
    EXPECT_EQ(0, memcmp(contents.data(), originalData.data(), fileSize));
}

//=============================================================================
// Additional edge-case tests
//=============================================================================

TEST_F(IBConnectionRecvfileTest, Recvfile_BadFileDescriptor_Fails)
{
    // Write to an invalid fd should fail gracefully (not crash)
    const int dataSize = 256;
    for (int i = 0; i < dataSize; i++) m_buffer[static_cast<size_t>(i)] = static_cast<char>(i);

    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, dataSize);

    bool result = conn.Recvfile(-1, dataSize);
    EXPECT_FALSE(result);
}

TEST_F(IBConnectionSendfileTest, Sendfile_BadFileDescriptor_Fails)
{
    // Read from an invalid fd should fail gracefully
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    // Size is within buffer limit, but fd is invalid → read() fails
    bool result = conn.Sendfile(-1, 0, 1024);
    EXPECT_FALSE(result);
}

TEST_F(IBConnectionWriteTest, WriteFinal_HappyPath)
{
    // WriteFinal calls Write + FinalizeWrite. Verify data is written correctly.
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    const std::string data = "response-payload";
    int n = conn.WriteFinal(const_cast<char*>(data.data()), static_cast<int>(data.size()));
    EXPECT_EQ(static_cast<int>(data.size()), n);

    // Verify buffer contents
    EXPECT_EQ(0, memcmp(m_buffer.data(), data.data(), data.size()));
}

TEST_F(IBConnectionWriteTest, WriteFinal_CalledTwice_SecondFails)
{
    // FinalizeWrite sets m_sendAttempted, so a second WriteFinal must fail.
    auto dummyQp = MakeDummyQp();
    IBConnection conn(dummyQp, m_buffer.data(), 0, 0);

    const std::string data1 = "first";
    int n1 = conn.WriteFinal(const_cast<char*>(data1.data()), static_cast<int>(data1.size()));
    EXPECT_EQ(static_cast<int>(data1.size()), n1);

    const std::string data2 = "second";
    int n2 = conn.WriteFinal(const_cast<char*>(data2.data()), static_cast<int>(data2.size()));
    EXPECT_EQ(-1, n2);
}
//---------------------------------------------------------------------------------------------------------------------
