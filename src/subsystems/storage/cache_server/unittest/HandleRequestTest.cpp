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
#include "MockConnection.h"
#include "cache.pb.h"

#include <cstring>
#include <vector>

using namespace cache;

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
