//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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
