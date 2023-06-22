//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
// This file implements the StorageInterceptUtilsTest.cpp
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptUtilsTest.h"
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
        result = StorageInterceptUtils::GetRemotePathPrefix("");
        bool isDynamicContainer = StorageInterceptConfig::GetInstance().IsDynamicContainerMount("");
        bool isIgnoreAccountName = StorageInterceptConfig::GetInstance().IsIgnoreAccountNameInCacheKey("");

        std::string output = !isIgnoreAccountName ? StorageInterceptConfig::GetInstance().GetStorageAccountName("") + "/" : "";
        output += !isDynamicContainer ? StorageInterceptConfig::GetInstance().GetBlobContainerName("") + "/" : "";
        EXPECT_EQ(result, output);

       //TODO:: test both cases when account name is ignored and otherwise, need to enable config override
    }
}