//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "AttributesCacheTest.h"
#include "RemoteCacheClient.h"
#include "LocalAttributesCache.h"
#include "StorageInterceptConfig.h"
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
AttributesCacheTest::AttributesCacheTest()
    : TestBase()
{
    uuid_t out;
    uuid_generate(out);
    char testId[37];
    uuid_unparse(out, testId);
    m_strTestId = testId;
    if (StorageInterceptConfig::GetInstance().GetAttributesCacheEnable(""))
    {
        if (StorageInterceptConfig::GetInstance().GetAttributesCacheUseLocalServer())
        {
            m_attributesCacheClient = make_shared<LocalAttributesCache>();
        }
        else if (StorageInterceptConfig::GetInstance().GetAttributesCacheUseRemoteServer())
        {
            m_attributesCacheClient = make_shared<RemoteCacheClient>();
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
void AttributesCacheTest::SetUp()
{
}
//---------------------------------------------------------------------------------------------------------------------
atomic_int AttributesCacheTest::m_fileCount = 0;
//---------------------------------------------------------------------------------------------------------------------
string AttributesCacheTest::GetRandFilename()
{
    return "test-" + m_strTestId + "-" + std::to_string(m_fileCount++);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(AttributesCacheTest, SimpleGetPutTest)
{
    if (this->m_attributesCacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    CacheStatus cacheStatus;
    FileAttributes fileAttributes;
    string strFileName = GetRandFilename();
    cacheStatus = m_attributesCacheClient->Get(strFileName, fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);
    cacheStatus = m_attributesCacheClient->Get(strFileName, fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND);

    fileAttributes.isDir = true;
    fileAttributes.size = 1;
    fileAttributes.aTime.tv_sec = 1;
    fileAttributes.mTime.tv_sec = 2;
    fileAttributes.cTime.tv_sec = 2;
    m_attributesCacheClient->Put(strFileName, fileAttributes);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(5);

    cacheStatus = m_attributesCacheClient->Get(strFileName, fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 1);
    ASSERT_EQ(fileAttributes.aTime.tv_sec, 1);
    ASSERT_EQ(fileAttributes.mTime.tv_sec, 2);
    ASSERT_EQ(fileAttributes.cTime.tv_sec, 2);

    string strFileName1 = GetRandFilename();
    fileAttributes.isDir = false;
    fileAttributes.size = 100;
    fileAttributes.aTime.tv_sec = 1;
    fileAttributes.mTime.tv_sec = 2;
    fileAttributes.cTime.tv_sec = 2;
    m_attributesCacheClient->Put(strFileName1, fileAttributes);

    string strFileName2 = GetRandFilename();
    fileAttributes.isDir = false;
    fileAttributes.size = 200;
    fileAttributes.aTime.tv_sec = 21;
    fileAttributes.mTime.tv_sec = 22;
    fileAttributes.cTime.tv_sec = 22;
    m_attributesCacheClient->Put(strFileName2, fileAttributes);

    string strFileName3 = GetRandFilename();
    fileAttributes.isDir = true;
    fileAttributes.size = 300;
    fileAttributes.aTime.tv_sec = 31;
    fileAttributes.mTime.tv_sec = 32;
    fileAttributes.cTime.tv_sec = 32;
    m_attributesCacheClient->Put(strFileName3, fileAttributes);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(5);

    cacheStatus = m_attributesCacheClient->Get(strFileName, fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 1);
    ASSERT_EQ(fileAttributes.aTime.tv_sec, 1);
    ASSERT_EQ(fileAttributes.mTime.tv_sec, 2);
    ASSERT_EQ(fileAttributes.cTime.tv_sec, 2);

    cacheStatus = m_attributesCacheClient->Get(strFileName1, fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 100);
    ASSERT_EQ(fileAttributes.aTime.tv_sec, 1);
    ASSERT_EQ(fileAttributes.mTime.tv_sec, 2);
    ASSERT_EQ(fileAttributes.cTime.tv_sec, 2);

    cacheStatus = m_attributesCacheClient->Get(strFileName2, fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 200);
    ASSERT_EQ(fileAttributes.aTime.tv_sec, 21);
    ASSERT_EQ(fileAttributes.mTime.tv_sec, 22);
    ASSERT_EQ(fileAttributes.cTime.tv_sec, 22);

    cacheStatus = m_attributesCacheClient->Get(strFileName3, fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 300);
    ASSERT_EQ(fileAttributes.aTime.tv_sec, 31);
    ASSERT_EQ(fileAttributes.mTime.tv_sec, 32);
    ASSERT_EQ(fileAttributes.cTime.tv_sec, 32);

    // Test PutAll API
    string strDirPath = GetRandFilename() + "/";
    vector<DirectoryEntry> dirEntryList;
    DirectoryEntry dirEntry;
    dirEntry.d_type = DT_DIR;
    dirEntry.d_size = 0;
    strcpy(dirEntry.d_name, "dir1");
    dirEntryList.push_back(dirEntry);
    dirEntry.d_type = DT_REG;
    dirEntry.d_size = 1024;
    strcpy(dirEntry.d_name, "file1");
    dirEntryList.push_back(dirEntry);
    m_attributesCacheClient->PutAll(strDirPath, dirEntryList, 0);
    sleep(5);

    cacheStatus = m_attributesCacheClient->Get(strDirPath + "dir1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);

    cacheStatus = m_attributesCacheClient->Get(strDirPath + "file1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 1024);

    cacheStatus = m_attributesCacheClient->Get(strDirPath + "dir2", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    cacheStatus = m_attributesCacheClient->Get(strDirPath + "file2", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::NOTFOUND_LOCKED);

    dirEntry.d_type = DT_DIR;
    dirEntry.d_size = 0;
    strcpy(dirEntry.d_name, "dir2");
    dirEntryList.push_back(dirEntry);
    dirEntry.d_type = DT_REG;
    dirEntry.d_size = 2048;
    strcpy(dirEntry.d_name, "file2");
    dirEntryList.push_back(dirEntry);
    m_attributesCacheClient->PutAll(strDirPath, dirEntryList, 2);
    sleep(5);

    cacheStatus = m_attributesCacheClient->Get(strDirPath + "dir1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);

    cacheStatus = m_attributesCacheClient->Get(strDirPath + "file1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 1024);

    cacheStatus = m_attributesCacheClient->Get(strDirPath + "dir2", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);

    cacheStatus = m_attributesCacheClient->Get(strDirPath + "file2", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 2048);

    m_attributesCacheClient->PutAll(strDirPath, dirEntryList, 0);
    sleep(5);

    cacheStatus = m_attributesCacheClient->Get(strDirPath + "dir1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);

    cacheStatus = m_attributesCacheClient->Get(strDirPath + "file1", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 1024);

    cacheStatus = m_attributesCacheClient->Get(strDirPath + "dir2", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 0);

    cacheStatus = m_attributesCacheClient->Get(strDirPath + "file2", fileAttributes);
    ASSERT_EQ(cacheStatus, CacheStatus::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 2048);
}
//---------------------------------------------------------------------------------------------------------------------
