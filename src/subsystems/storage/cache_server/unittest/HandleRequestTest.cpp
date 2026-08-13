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
