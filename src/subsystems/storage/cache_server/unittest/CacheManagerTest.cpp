//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include <stdlib.h>
#include "CacheManagerTest.h"
#include "CacheManager.h"
#include "Constants.h"
#include "Utils.h"
//---------------------------------------------------------------------------------------------------------------------
CacheManagerTest::CacheManagerTest()
{
    // Set environment variables
    ::setenv("CACHESERVER_HOSTNAME", "unittesthost", 1);
    ::setenv("CACHESERVER_METRICS_PORT", "9096", 1);
    SetCacheRootPath("/tmp/cacheManagerUnitTest");
    InitializeUtils();
}
//---------------------------------------------------------------------------------------------------------------------
void CacheManagerTest::SetUp()
{
}
//---------------------------------------------------------------------------------------------------------------------
void CacheManagerTest::AddCacheEntry(
    const std::string& fileName,
    uint64_t fileSize,
    time_t expiryTime,
    const std::string& groupId)
{
    bool ret = CacheManager::GetInstance().InitCacheInfo(
        fileName,
        true /* ignore lock */,
        fileSize);
    ASSERT_TRUE(ret);

    google::protobuf::Map<std::string, std::string> emptyMetadata;
    ret = CacheManager::GetInstance().UpdateCacheInfo(
        fileName,
        fileSize,
        emptyMetadata,
        expiryTime,
        groupId);
    ASSERT_TRUE(ret);
}
//---------------------------------------------------------------------------------------------------------------------
void CacheManagerTest::ValidateCacheEntry(
    const std::string& fileName,
    uint64_t fileSize,
    time_t expiryTime,
    const std::string& groupId)
{
    CacheManager::CacheInfo cacheInfo = CacheManager::GetInstance().GetCacheInfo(fileName);
    ASSERT_EQ(cacheInfo.m_fileSize, fileSize);
    ASSERT_EQ(cacheInfo.m_status, CacheManager::CacheStatus::COMPLETED);
    ASSERT_EQ(cacheInfo.m_expiryTime, expiryTime);
    ASSERT_EQ(cacheInfo.m_groupId, groupId);
}
//---------------------------------------------------------------------------------------------------------------------
void CacheManagerTest::ValidateCacheEntryNotExists(const std::string& fileName)
{
    CacheManager::CacheInfo cacheInfo = CacheManager::GetInstance().GetCacheInfo(fileName);
    ASSERT_EQ(cacheInfo.m_status, CacheManager::CacheStatus::NOTFOUND);
}
//---------------------------------------------------------------------------------------------------------------------
#define RESET_CACHE_MANAGER() \
    CacheManager::GetInstance().m_vCacheInfoMap.clear(); \
    CacheManager::GetInstance().m_vCacheInfoMap.resize(NUM_MAPS); \
    CacheManager::GetInstance().m_vCacheTTLMap.clear(); \
    CacheManager::GetInstance().m_vCacheTTLMap.resize(NUM_MAPS); \
    CacheManager::GetInstance().m_vCacheGroupIDMap.clear(); \
    CacheManager::GetInstance().m_vCacheGroupIDMap.resize(NUM_MAPS); \
    CacheManager::GetInstance().m_groupIdQueue = {}; \
    CacheManager::GetInstance().m_totalCachedSize = 0;
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheManagerTest, PutAndGetCacheEntry)
{
    std::string fileName = "/test/putAndGetCacheEntry";
    uint64_t fileSize = 100;
    time_t expiryTime = 0;
    std::string groupId = "testGroup";

    AddCacheEntry(fileName, fileSize, expiryTime, groupId);
    ValidateCacheEntry(fileName, fileSize, expiryTime, groupId);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheManagerTest, TakeAndLoadSnapshot)
{
    std::string fileName = "/test/takeAndLoadSnapshot";
    uint64_t fileSize = 100;
    time_t expiryTime = 0;
    std::string groupId = "testGroup";

    // Add cache entry
    AddCacheEntry(fileName, fileSize, expiryTime, groupId);
    ValidateCacheEntry(fileName, fileSize, expiryTime, groupId);
    ASSERT_GT(CacheManager::GetInstance().m_totalCachedSize, 0);

    // Take snapshot
    std::shared_ptr<snapshot::Snapshot> snapshot = CacheManager::GetInstance().TakeSnapshot();
    ASSERT_NE(snapshot, nullptr);

    // Reset cache manager data. The cache entry should be removed
    RESET_CACHE_MANAGER();
    ValidateCacheEntryNotExists(fileName);
    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize, 0);

    // Load snapshot
    CacheManager::GetInstance().InitializeFromSnapshot(*snapshot);

    // Validate cache entry added after load from snapshot
    ValidateCacheEntry(fileName, fileSize, expiryTime, groupId);
    ASSERT_GT(CacheManager::GetInstance().m_totalCachedSize, 0);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheManagerTest, RembemberPendingGroupIdDeletionAfterSnapshotLoad)
{
    RESET_CACHE_MANAGER();
    CacheManager::GetInstance().m_groupIdQueue.push("testGroup1");
    CacheManager::GetInstance().m_groupIdQueue.push("testGroup2");
    ASSERT_EQ(CacheManager::GetInstance().m_groupIdQueue.size(), 2);

    // Take snapshot
    std::shared_ptr<snapshot::Snapshot> snapshot = CacheManager::GetInstance().TakeSnapshot();
    ASSERT_NE(snapshot, nullptr);
    RESET_CACHE_MANAGER();

    // Load snapshot
    CacheManager::GetInstance().InitializeFromSnapshot(*snapshot);

    // Validate pending group id deletion
    ASSERT_EQ(CacheManager::GetInstance().m_groupIdQueue.size(), 2);
    ASSERT_EQ(CacheManager::GetInstance().m_groupIdQueue.front(), "testGroup1");
    CacheManager::GetInstance().m_groupIdQueue.pop();
    ASSERT_EQ(CacheManager::GetInstance().m_groupIdQueue.front(), "testGroup2");
}
//---------------------------------------------------------------------------------------------------------------------