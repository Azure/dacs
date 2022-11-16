//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "LocalAttributesCacheTest.h"
#include "LocalAttributesCache.h"
#include "StorageInterceptConfig.h"
#include <algorithm>
#include <vector>
#include <unistd.h>
#include <iostream>
#include <uuid/uuid.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
LocalAttributesCacheTest::LocalAttributesCacheTest()
    : TestBase()
{
    uuid_t out;
    uuid_generate(out);
    char testId[37];
    uuid_unparse(out, testId);
    m_strTestId = testId;
    if (StorageInterceptConfig::GetInstance().GetAttributesCacheEnable("") &&
        StorageInterceptConfig::GetInstance().GetAttributesCacheUseLocalServer())
    {
        m_attributesCacheClient = std::make_shared<LocalAttributesCache>();
    }
}
//---------------------------------------------------------------------------------------------------------------------
void LocalAttributesCacheTest::SetUp()
{
}
//---------------------------------------------------------------------------------------------------------------------
atomic_int LocalAttributesCacheTest::m_fileCount = 0;
//---------------------------------------------------------------------------------------------------------------------
string LocalAttributesCacheTest::GetRandFilename()
{
    return "test-" + m_strTestId + "-" + std::to_string(m_fileCount++);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(LocalAttributesCacheTest, SimpleGetPutTest)
{
    if (this->m_attributesCacheClient == nullptr)
    {
        GTEST_SKIP();
    }

    IAttributesCache::CacheStatus cacheStatus;
    FileAttributes fileAttributes;
    string strFileName = GetRandFilename();
    cacheStatus = m_attributesCacheClient->Get(strFileName, fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::NOTFOUND_LOCKED);
    cacheStatus = m_attributesCacheClient->Get(strFileName, fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::NOTFOUND);

    fileAttributes.isDir = true;
    fileAttributes.size = 1;
    fileAttributes.aTime.tv_sec = 1;
    fileAttributes.mTime.tv_sec = 2;
    fileAttributes.cTime.tv_sec = 3;
    m_attributesCacheClient->Put(strFileName, fileAttributes);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(5);

    cacheStatus = m_attributesCacheClient->Get(strFileName, fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::SUCCESS);
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
    fileAttributes.cTime.tv_sec = 3;
    m_attributesCacheClient->Put(strFileName1, fileAttributes);

    string strFileName2 = GetRandFilename();
    fileAttributes.isDir = false;
    fileAttributes.size = 200;
    fileAttributes.aTime.tv_sec = 21;
    fileAttributes.mTime.tv_sec = 22;
    fileAttributes.cTime.tv_sec = 23;
    m_attributesCacheClient->Put(strFileName2, fileAttributes);

    string strFileName3 = GetRandFilename();
    fileAttributes.isDir = true;
    fileAttributes.size = 300;
    fileAttributes.aTime.tv_sec = 31;
    fileAttributes.mTime.tv_sec = 32;
    fileAttributes.cTime.tv_sec = 33;
    m_attributesCacheClient->Put(strFileName3, fileAttributes);

    // wait for async upload to complete 
    // TODO: make this deterministic
    sleep(5);

    cacheStatus = m_attributesCacheClient->Get(strFileName, fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 1);
    ASSERT_EQ(fileAttributes.aTime.tv_sec, 1);
    ASSERT_EQ(fileAttributes.mTime.tv_sec, 2);
    ASSERT_EQ(fileAttributes.cTime.tv_sec, 2);

    cacheStatus = m_attributesCacheClient->Get(strFileName1, fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 100);
    ASSERT_EQ(fileAttributes.aTime.tv_sec, 1);
    ASSERT_EQ(fileAttributes.mTime.tv_sec, 2);
    ASSERT_EQ(fileAttributes.cTime.tv_sec, 2);

    cacheStatus = m_attributesCacheClient->Get(strFileName2, fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, false);
    ASSERT_EQ(fileAttributes.size, 200);
    ASSERT_EQ(fileAttributes.aTime.tv_sec, 21);
    ASSERT_EQ(fileAttributes.mTime.tv_sec, 22);
    ASSERT_EQ(fileAttributes.cTime.tv_sec, 22);

    cacheStatus = m_attributesCacheClient->Get(strFileName3, fileAttributes);
    ASSERT_EQ(cacheStatus, IAttributesCache::SUCCESS);
    ASSERT_EQ(fileAttributes.isDir, true);
    ASSERT_EQ(fileAttributes.size, 300);
    ASSERT_EQ(fileAttributes.aTime.tv_sec, 31);
    ASSERT_EQ(fileAttributes.mTime.tv_sec, 32);
    ASSERT_EQ(fileAttributes.cTime.tv_sec, 32);
}
//---------------------------------------------------------------------------------------------------------------------
