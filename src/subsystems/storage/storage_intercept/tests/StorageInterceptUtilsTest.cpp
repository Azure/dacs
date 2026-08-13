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

// This file implements the StorageInterceptUtilsTest.cpp
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptUtilsTest.h"
#include "StorageInterceptConstants.h"
#include "gtest/gtest.h"
//--------------------------------------------------------------------------------------------------------------------------------------------
StorageInterceptUtilsTest::StorageInterceptUtilsTest()
    : TestBase()
{
}
//---------------------------------------------------------------------------------------------------------------------
void StorageInterceptUtilsTest::SetUp()
{

}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(StorageInterceptUtilsTest, CombinePathTest)
{
    EXPECT_EQ(
        StorageInterceptUtils::CombinePath("foo", "bar"),
        "foo/bar");

    EXPECT_EQ(
        StorageInterceptUtils::CombinePath("foo/", "bar"),
        "foo/bar");

    EXPECT_EQ(
        StorageInterceptUtils::CombinePath("foo/", "/bar"),
        "foo/bar");

    EXPECT_EQ(
        StorageInterceptUtils::CombinePath("foo", "/bar"),
        "foo/bar");

    EXPECT_EQ(
        StorageInterceptUtils::CombinePath("foo/", ""),
        "foo");
}
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST_F(StorageInterceptUtilsTest, GetRelativePathTest)
{
    std::string result;

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/foo/bar", result),
        true);
    EXPECT_EQ(result, "bar");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo/bar", "/foo/bar/aa/bb", result),
        true);
    EXPECT_EQ(result, "aa/bb");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/foo", result),
        true);
    EXPECT_EQ(result, "");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/foo/", result),
        true);
    EXPECT_EQ(result, "");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/bar", result),
        false);
    EXPECT_EQ(result, "");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/foot/bar", result),
        false);
    EXPECT_EQ(result, "");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/aa/foo/bar", result),
        false);
    EXPECT_EQ(result, "");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo", "/foo//bar//aa", result),
        true);
    EXPECT_EQ(result, "bar/aa");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo/bar", "/foo//bar", result),
        true);
    EXPECT_EQ(result, "");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo/bar", "/foo//bar//aa/bb//cc", result),
        true);
    EXPECT_EQ(result, "aa/bb/cc");

    EXPECT_EQ(
        StorageInterceptUtils::GetRelativePath("/foo/bar", "/foo//bart", result),
        false);
    EXPECT_EQ(result, "");
}
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST_F(StorageInterceptUtilsTest, GetRemotePathPrefixBlobTest)
{
    std::string result;

    if (!m_bUsingMock)
    {
        OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobDynamicContainer, "false");
        OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobStorageAccName, "acc");
        OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobContainerName, "cont");
        result = StorageInterceptUtils::GetRemotePathPrefix("");
        ASSERT_EQ(result, "acc/cont/");

        OverrideConfig(StorageInterceptConstants::ConfigNameAzIgnoreAccountNameInCacheKey, "true");
        result = StorageInterceptUtils::GetRemotePathPrefix("");
        ASSERT_EQ(result, "$accountNameOverride$/cont/");
        OverrideConfig(StorageInterceptConstants::ConfigNameAzIgnoreAccountNameInCacheKey, "false");

        OverrideConfig(StorageInterceptConstants::ConfigNameAzBlobDynamicContainer, "true");
        result = StorageInterceptUtils::GetRemotePathPrefix("");
        ASSERT_EQ(result, "acc/");

        OverrideConfig(StorageInterceptConstants::ConfigNameAzIgnoreAccountNameInCacheKey, "true");
        result = StorageInterceptUtils::GetRemotePathPrefix("");
        ASSERT_EQ(result, "$accountNameOverride$/");
        OverrideConfig(StorageInterceptConstants::ConfigNameAzIgnoreAccountNameInCacheKey, "false");        
    }
}