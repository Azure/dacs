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

// Unit tests for HandlePutAttribute.
//---------------------------------------------------------------------------------------------------------------------
#include <gtest/gtest.h>

#include "PutAttributeRequest.h"
#include "MockConnection.h"
#include "cache.pb.h"

using namespace cache;

//---------------------------------------------------------------------------------------------------------------------
// An empty PutAttributeRequest (0 file attributes) must not crash the handler.
TEST(PutAttributeRequestTest, HandlesEmptyFileAttributesWithoutCrash)
{
    PutAttributeRequest request;
    request.set_requestid("empty-put-attributes");
    ASSERT_EQ(request.fileattributes_size(), 0);

    MockConnection conn;
    EXPECT_TRUE(HandlePutAttribute(conn, request));
}
//---------------------------------------------------------------------------------------------------------------------
