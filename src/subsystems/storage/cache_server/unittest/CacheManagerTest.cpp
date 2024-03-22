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
    ::setenv("CACHESERVER_EVICTION_ENABLED", "true", 1);
    ::setenv("CACHESERVER_EVICTION_LAST_ACCESS_THRESHOLD", "5s", 1);
    ::setenv("CACHESERVER_EVICTION_PASS_FREQUENCY", "1s", 1);
    ::setenv("CACHESERVER_EVICTION_SIZE_THRESHOLD", "0.5", 1);
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
    // Take the eviction thread mutex to prevent races
    {
        std::lock_guard<std::mutex> lock(CacheManager::GetInstance().m_mutexEviction);
        RESET_CACHE_MANAGER();
    }
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
    // Take the eviction thread mutex to prevent races
    {
        std::lock_guard<std::mutex> lock(CacheManager::GetInstance().m_mutexEviction);
        RESET_CACHE_MANAGER();
    }
    CacheManager::GetInstance().m_groupIdQueue.push("testGroup1");
    CacheManager::GetInstance().m_groupIdQueue.push("testGroup2");
    ASSERT_EQ(CacheManager::GetInstance().m_groupIdQueue.size(), 2);

    // Take snapshot
    std::shared_ptr<snapshot::Snapshot> snapshot = CacheManager::GetInstance().TakeSnapshot();
    ASSERT_NE(snapshot, nullptr);
    // Take the eviction thread mutex to prevent races
    {
        std::lock_guard<std::mutex> lock(CacheManager::GetInstance().m_mutexEviction);
        RESET_CACHE_MANAGER();
    }

    // Load snapshot
    CacheManager::GetInstance().InitializeFromSnapshot(*snapshot);

    // Validate pending group id deletion
    ASSERT_EQ(CacheManager::GetInstance().m_groupIdQueue.size(), 2);
    ASSERT_EQ(CacheManager::GetInstance().m_groupIdQueue.front(), "testGroup1");
    CacheManager::GetInstance().m_groupIdQueue.pop();
    ASSERT_EQ(CacheManager::GetInstance().m_groupIdQueue.front(), "testGroup2");
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheManagerTest, TestParseTimeIntervalString)
{
    uint64_t nValue = CacheManager::GetInstance().ParseTimeIntervalString("1s");
    ASSERT_EQ(1, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("3m15s");
    ASSERT_EQ(195, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("15s3m");
    ASSERT_EQ(195, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("20d15h2m19s");
    ASSERT_EQ(1782139, nValue);

    // Test invalid strings
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("hdsm");
    ASSERT_EQ(0, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("20dh2m12s");
    ASSERT_EQ(0, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("2ms");
    ASSERT_EQ(0, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("hd15m2s");
    ASSERT_EQ(0, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("2m?&16s");
    ASSERT_EQ(0, nValue);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheManagerTest, TestAccessTimeUpdates)
{
    std::string fileName = "/test/access";
    uint64_t fileSize = 100;
    time_t expiryTime = 0;
    std::string groupId = "accessGroup";

    AddCacheEntry(fileName, fileSize, expiryTime, groupId);
    ValidateCacheEntry(fileName, fileSize, expiryTime, groupId);

    time_t lastAccessTime = CacheManager::GetInstance().GetCacheInfo(fileName).m_accessTimestamp;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    time_t newAccessTime = CacheManager::GetInstance().GetCacheInfo(fileName).m_accessTimestamp;
    ASSERT_NE(lastAccessTime, newAccessTime);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheManagerTest, TestEviction)
{
    // Take the eviction thread mutex to prevent races
    {
        std::lock_guard<std::mutex> lock(CacheManager::GetInstance().m_mutexEviction);
        RESET_CACHE_MANAGER();
    }

    std::string fileNamePrefix = "/test/eviction";
    uint64_t fileSize = 1024*1024*1024;
    time_t expiryTime = 0;
    std::string groupId = "evictionGroup";

    // Add 5 files to the cache
    for (int i = 0; i < 5; i++)
    {
        AddCacheEntry(fileNamePrefix + std::to_string(i), fileSize, expiryTime, groupId);
        ValidateCacheEntry(fileNamePrefix + std::to_string(i), fileSize, expiryTime, groupId);
        ASSERT_GT(CacheManager::GetInstance().m_totalCachedSize, 0);
    }

    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize, 5 * fileSize);

    // We have set eviction window=5s and interval=1s via env variables
    // Sleep for an interval < eviction window, and touch all files
    std::this_thread::sleep_for(std::chrono::seconds(3));
    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize, 5 * fileSize);
    for (int i = 0; i < 5; i++)
    {
        ValidateCacheEntry(fileNamePrefix + std::to_string(i), fileSize, expiryTime, groupId);
    }

    // Sleep for an interval < eviction window, and touch a subset of the files
    std::this_thread::sleep_for(std::chrono::seconds(4));
    ValidateCacheEntry(fileNamePrefix + "1", fileSize, expiryTime, groupId);
    ValidateCacheEntry(fileNamePrefix + "2", fileSize, expiryTime, groupId);

    // Now sleep the remaining seconds left in the eviction window
    std::this_thread::sleep_for(std::chrono::seconds(2));

    //No Eviction should happen as cache size is below threshold
    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize, 5 * fileSize);

    // Add two more files to the cache, so that the total size exceeds the threshold
    for (int i = 5; i < 7; i++)
    {
        AddCacheEntry(fileNamePrefix + std::to_string(i), fileSize, expiryTime, groupId);
    }

    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize, 7 * fileSize);

    //Now sleep for another eviction pass to happen
    std::this_thread::sleep_for(std::chrono::seconds(2));

    //Older files should be evicted now as both the size and time threshold are met
    ValidateCacheEntryNotExists(fileNamePrefix + "0");
    ValidateCacheEntry(fileNamePrefix + "1", fileSize, expiryTime, groupId);
    ValidateCacheEntry(fileNamePrefix + "2", fileSize, expiryTime, groupId);
    ValidateCacheEntryNotExists(fileNamePrefix + "3");
    ValidateCacheEntryNotExists(fileNamePrefix + "4");
    ValidateCacheEntry(fileNamePrefix + "5", fileSize, expiryTime, groupId);
    ValidateCacheEntry(fileNamePrefix + "6", fileSize, expiryTime, groupId);

    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize, 4 * fileSize);
}
//---------------------------------------------------------------------------------------------------------------------