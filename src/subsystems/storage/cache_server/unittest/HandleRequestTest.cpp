//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// Unit test for HandleRequest: verifies that a client-declared protobuf size
// exceeding MAX_REQUEST_PROTO_SIZE is rejected before any body allocation.
//---------------------------------------------------------------------------------------------------------------------
#include <gtest/gtest.h>

#include "HandleRequest.h"
#include "ConnectionUtils.h"
#include "Constants.h"
#include "IConnection.h"
#include "cache.pb.h"

#include <cstring>
#include <vector>

using namespace cache;

//---------------------------------------------------------------------------------------------------------------------
// In-memory IConnection that serves a fixed byte buffer to Read().
class MockConnection : public IConnection
{
public:
    explicit MockConnection(std::vector<char> readData) : m_readData(std::move(readData)) {}

    int Read(char* buffer, int size) override
    {
        int available = static_cast<int>(m_readData.size()) - m_readPos;
        if (size <= 0 || available <= 0)
        {
            return 0; // simulate EOF / closed peer
        }

        int toRead = std::min(size, available);
        std::memcpy(buffer, m_readData.data() + m_readPos, toRead);
        m_readPos += toRead;
        return toRead;
    }

    int Write(char* buffer, int size) override { return size; }
    int WriteFinal(char* buffer, int size) override { return size; }
    bool Sendfile(int, uint64_t, uint64_t) override { return true; }
    bool Recvfile(int, uint64_t) override { return true; }
    int GetId() override { return 4242; }

private:
    std::vector<char> m_readData;
    int m_readPos = 0;
};
//---------------------------------------------------------------------------------------------------------------------
// Build a raw metadata-only buffer encoding an arbitrary declared proto size.
static std::vector<char> BuildMetadata(uint32_t declaredSize)
{
    std::vector<char> wire(REQUEST_METADATA_SIZE);
    ConvertIntToBytes(declaredSize, wire.data(), 0, REQUEST_METADATA_SIZE);
    return wire;
}
//---------------------------------------------------------------------------------------------------------------------
// A declared proto size larger than the maximum is rejected before any body read.
TEST(HandleRequestTest, FailsWhenProtoSizeExceedsMax)
{
    Request::TypeCase type = Request::TYPE_NOT_SET;
    ResponseCode responseCode{};
    std::size_t fileSize = 12345; // sentinel to detect unexpected writes

    MockConnection conn(BuildMetadata(MAX_REQUEST_PROTO_SIZE + 1));
    EXPECT_FALSE(HandleRequest(conn, type, responseCode, fileSize));
}
//---------------------------------------------------------------------------------------------------------------------
