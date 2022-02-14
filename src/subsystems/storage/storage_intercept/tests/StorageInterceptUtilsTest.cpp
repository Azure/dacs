//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
// This file implements the StorageInterceptUtilsTest.cpp
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptUtils.h"
#include "gtest/gtest.h"
//--------------------------------------------------------------------------------------------------------------------------------------------
TEST(StorageInterceptUtilsTest, CombinePathTest)
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
TEST(StorageInterceptUtilsTest, GetRelativePathTest)
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
}