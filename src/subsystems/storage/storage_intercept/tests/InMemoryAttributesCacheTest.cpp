//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "InMemoryAttributesCacheTest.h"
#include "InMemoryAttributesCache.h"
#include <algorithm>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <uuid/uuid.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
InMemoryAttributesCacheTest::InMemoryAttributesCacheTest()
    : TestBase()
{
}
//---------------------------------------------------------------------------------------------------------------------
void InMemoryAttributesCacheTest::SetUp()
{
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(InMemoryAttributesCacheTest, InMemoryAttributesCacheTest)
{
    // Test success
    InMemoryAttributesCache attributesCache1(2);
    FileAttributes fileAttributes;
    fileAttributes.isDir = true;
    fileAttributes.size = 0;
    attributesCache1.Put("remote/dir1", fileAttributes);
    fileAttributes.isDir = false;
    fileAttributes.size = 100;
    attributesCache1.Put("remote/file1", fileAttributes);

    IAttributesCache::CacheStatus cacheStatus = attributesCache1.Get("remote/dir1", fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);

    cacheStatus = attributesCache1.Get("remote/file1", fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 100);

    // Test no cache
    InMemoryAttributesCache attributesCache2(0);
    fileAttributes.isDir = true;
    fileAttributes.size = 0;
    attributesCache2.Put("remote/dir1", fileAttributes);
    fileAttributes.isDir = false;
    fileAttributes.size = 100;
    attributesCache2.Put("remote/file1", fileAttributes);

    cacheStatus = attributesCache2.Get("remote/dir1", fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::NOTFOUND_LOCKED);

    cacheStatus = attributesCache2.Get("remote/file1", fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::NOTFOUND_LOCKED);

    // Test cache limit
    InMemoryAttributesCache attributesCache3(1);
    fileAttributes.isDir = true;
    fileAttributes.size = 0;
    attributesCache3.Put("remote/dir1", fileAttributes);
    fileAttributes.isDir = false;
    fileAttributes.size = 100;
    attributesCache3.Put("remote/file1", fileAttributes);

    cacheStatus = attributesCache3.Get("remote/dir1", fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);

    cacheStatus = attributesCache3.Get("remote/file1", fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::NOTFOUND_LOCKED);
}
//---------------------------------------------------------------------------------------------------------------------