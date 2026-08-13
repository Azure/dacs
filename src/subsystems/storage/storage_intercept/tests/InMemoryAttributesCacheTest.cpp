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

#include "InMemoryAttributesCacheTest.h"
#include "InMemoryAttributesCache.h"
#include <algorithm>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <uuid/uuid.h>
#include <dirent.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace cacheutils;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
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
    fileAttributes.version = "uuid-v1";
    attributesCache1.Put("remote/dir1", fileAttributes);
    fileAttributes.isDir = false;
    fileAttributes.size = 100;
    fileAttributes.version = "uuid-v2";
    attributesCache1.Put("remote/file1", fileAttributes);

    CacheStatus cacheStatus = attributesCache1.Get("remote/dir1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);
    ASSERT_EQ(fileAttributes.version, "uuid-v1");

    cacheStatus = attributesCache1.Get("remote/file1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 100);
    ASSERT_EQ(fileAttributes.version, "uuid-v2");

    // Test no cache
    InMemoryAttributesCache attributesCache2(0);
    fileAttributes.isDir = true;
    fileAttributes.size = 0;
    attributesCache2.Put("remote/dir1", fileAttributes);
    fileAttributes.isDir = false;
    fileAttributes.size = 100;
    attributesCache2.Put("remote/file1", fileAttributes);

    cacheStatus = attributesCache2.Get("remote/dir1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    cacheStatus = attributesCache2.Get("remote/file1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    // Test cache limit
    InMemoryAttributesCache attributesCache3(1);
    fileAttributes.isDir = true;
    fileAttributes.size = 0;
    attributesCache3.Put("remote/dir1", fileAttributes);
    fileAttributes.isDir = false;
    fileAttributes.size = 100;
    attributesCache3.Put("remote/file1", fileAttributes);

    cacheStatus = attributesCache3.Get("remote/dir1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);

    cacheStatus = attributesCache3.Get("remote/file1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    // Test PutAll API
    InMemoryAttributesCache attributesCache4(10);

    string strDirPath = "remote/";
    vector<DirectoryEntry> dirEntryList;
    DirectoryEntry dirEntry;
    dirEntry.d_type = DT_DIR;
    dirEntry.d_size = 0;
    strcpy(dirEntry.d_name, "dir1");
    strncpy(dirEntry.d_version, "uuid-dir1", sizeof(dirEntry.d_version) - 1);
    dirEntry.d_version[sizeof(dirEntry.d_version) - 1] = '\0';
    dirEntryList.push_back(dirEntry);
    dirEntry.d_type = DT_REG;
    dirEntry.d_size = 1024;
    strcpy(dirEntry.d_name, "file1");
    strncpy(dirEntry.d_version, "uuid-file1", sizeof(dirEntry.d_version) - 1);
    dirEntry.d_version[sizeof(dirEntry.d_version) - 1] = '\0';
    dirEntryList.push_back(dirEntry);
    attributesCache4.PutAll(strDirPath, dirEntryList, 0);

    cacheStatus = attributesCache4.Get(strDirPath + "dir1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);
    ASSERT_EQ(fileAttributes.version, "uuid-dir1");

    cacheStatus = attributesCache4.Get(strDirPath + "file1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 1024);
    ASSERT_EQ(fileAttributes.version, "uuid-file1");

    cacheStatus = attributesCache4.Get(strDirPath + "dir2", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    cacheStatus = attributesCache4.Get(strDirPath + "file2", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    dirEntry.d_type = DT_DIR;
    dirEntry.d_size = 0;
    strcpy(dirEntry.d_name, "dir2");
    dirEntry.d_version[0] = '\0';
    dirEntryList.push_back(dirEntry);
    dirEntry.d_type = DT_REG;
    dirEntry.d_size = 2048;
    strcpy(dirEntry.d_name, "file2");
    dirEntry.d_version[0] = '\0';
    dirEntryList.push_back(dirEntry);
    attributesCache4.PutAll(strDirPath, dirEntryList, 2);

    cacheStatus = attributesCache4.Get(strDirPath + "dir1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);
    ASSERT_EQ(fileAttributes.version, "uuid-dir1");

    cacheStatus = attributesCache4.Get(strDirPath + "file1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 1024);
    ASSERT_EQ(fileAttributes.version, "uuid-file1");

    cacheStatus = attributesCache4.Get(strDirPath + "dir2", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);
    ASSERT_TRUE(fileAttributes.version.empty());

    cacheStatus = attributesCache4.Get(strDirPath + "file2", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 2048);
    ASSERT_TRUE(fileAttributes.version.empty());

    attributesCache4.PutAll(strDirPath, dirEntryList, 0);

    cacheStatus = attributesCache4.Get(strDirPath + "dir1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);
    ASSERT_EQ(fileAttributes.version, "uuid-dir1");

    cacheStatus = attributesCache4.Get(strDirPath + "file1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 1024);
    ASSERT_EQ(fileAttributes.version, "uuid-file1");

    cacheStatus = attributesCache4.Get(strDirPath + "dir2", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);
    ASSERT_TRUE(fileAttributes.version.empty());

    cacheStatus = attributesCache4.Get(strDirPath + "file2", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 2048);
    ASSERT_TRUE(fileAttributes.version.empty());
}
//---------------------------------------------------------------------------------------------------------------------