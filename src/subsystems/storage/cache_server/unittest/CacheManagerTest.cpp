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

#include <stdlib.h>
#include "CacheManagerTest.h"
#include "CacheManager.h"
#include "Constants.h"
#include "Utils.h"
#include "Logger.h"
#include "ServerLocatorCache.h"
#include "FlatMapIndex.h"
#include <chrono>
#include <memory>
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
        static_cast<uint32_t>(expiryTime),
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
    CacheManager::GetInstance().m_totalCachedSize = 0; \
    CacheManager::GetInstance().m_bDynamicEvictionEnabled = false; \
    CacheManager::GetInstance().m_dEvictionSizeThreshold = 0.5;
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
TEST_F(CacheManagerTest, CleanupWorkerStartsWithConfiguredInterval)
{
    CacheManager cacheManager;
    const std::string fileName = "/test/configuredCleanupInterval";
    const std::string groupId = "configuredCleanupIntervalGroup";

    ASSERT_FALSE(cacheManager.m_cleanupThread);
    cacheManager.StartCacheCleanupWorker(1);

    ASSERT_TRUE(cacheManager.InitCacheInfo(fileName, true, 100));
    ASSERT_TRUE(cacheManager.UpdateCacheInfo(
        fileName, 100, google::protobuf::Map<std::string, std::string>(), 0, groupId));
    ASSERT_TRUE(cacheManager.DeleteCacheEntryByGroupID(groupId));

    const auto startTime = std::chrono::steady_clock::now();
    const auto deadline = startTime + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline &&
           cacheManager.GetCacheInfo(fileName).m_status != CacheManager::CacheStatus::NOTFOUND)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    const auto elapsedMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();
    const bool deleted = cacheManager.GetCacheInfo(fileName).m_status == CacheManager::CacheStatus::NOTFOUND;

    EXPECT_TRUE(deleted) << "Cleanup worker did not use the configured startup interval";
    EXPECT_LT(elapsedMillis, 5000);
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
    ASSERT_GT(CacheManager::GetInstance().m_totalCachedSize.load(), 0ULL);

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
    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize.load(), 0ULL);

    // Load snapshot
    CacheManager::GetInstance().InitializeFromSnapshot(*snapshot);

    // Validate cache entry added after load from snapshot
    ValidateCacheEntry(fileName, fileSize, expiryTime, groupId);
    ASSERT_GT(CacheManager::GetInstance().m_totalCachedSize.load(), 0ULL);
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
    ASSERT_EQ(CacheManager::GetInstance().m_groupIdQueue.size(), 2U);

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
    ASSERT_EQ(CacheManager::GetInstance().m_groupIdQueue.size(), 2U);
    ASSERT_EQ(CacheManager::GetInstance().m_groupIdQueue.front(), "testGroup1");
    CacheManager::GetInstance().m_groupIdQueue.pop();
    ASSERT_EQ(CacheManager::GetInstance().m_groupIdQueue.front(), "testGroup2");
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheManagerTest, TestParseTimeIntervalString)
{
    uint64_t nValue = CacheManager::GetInstance().ParseTimeIntervalString("1s");
    ASSERT_EQ(1ULL, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("3m15s");
    ASSERT_EQ(195ULL, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("15s3m");
    ASSERT_EQ(195ULL, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("20d15h2m19s");
    ASSERT_EQ(1782139ULL, nValue);

    // Test invalid strings
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("hdsm");
    ASSERT_EQ(0ULL, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("20dh2m12s");
    ASSERT_EQ(0ULL, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("2ms");
    ASSERT_EQ(0ULL, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("hd15m2s");
    ASSERT_EQ(0ULL, nValue);
    nValue = CacheManager::GetInstance().ParseTimeIntervalString("2m?&16s");
    ASSERT_EQ(0ULL, nValue);
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
        ASSERT_GT(CacheManager::GetInstance().m_totalCachedSize.load(), 0ULL);
    }

    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize.load(), 5 * fileSize);

    // We have set eviction window=5s and interval=1s via env variables
    // Sleep for an interval < eviction window, and touch all files
    std::this_thread::sleep_for(std::chrono::seconds(3));
    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize.load(), 5 * fileSize);
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
    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize.load(), 5 * fileSize);

    // Add two more files to the cache, so that the total size exceeds the threshold
    for (int i = 5; i < 7; i++)
    {
        AddCacheEntry(fileNamePrefix + std::to_string(i), fileSize, expiryTime, groupId);
    }

    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize.load(), 7 * fileSize);

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

    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize.load(), 4 * fileSize);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheManagerTest, TestDynamicEviction)
{
    // This test validates the actual bucket iteration logic by directly manipulating
    // access timestamps to create multiple hour buckets
    
    // Take the eviction thread mutex to prevent races
    {
        std::lock_guard<std::mutex> lock(CacheManager::GetInstance().m_mutexEviction);
        RESET_CACHE_MANAGER();
    }

    // Enable dynamic eviction for this test only
    CacheManager::GetInstance().m_bDynamicEvictionEnabled = true;
    CacheManager::GetInstance().m_dEvictionSizeThreshold = 70;
    CacheManager::GetInstance().m_dEvictionSizeTarget = 40; // 40% of cache size limit

    // Configure eviction settings
    CacheManager::GetInstance().SetCacheSizeLimit(1000 * 1024 * 1024); // 1GB limit for this test
    uint64_t fileSize = 200 * 1024 * 1024; // 200MB files
        
    // Create files and directly manipulate their access timestamps to span multiple hours
    time_t baseTime = 1640995200; // Fixed base time
    std::vector<std::string> fileNames;
    
    // Create 4 files with access times in different hour buckets
    for (int i = 0; i < 4; i++)
    {
        std::string fileName = "multi_bucket_file_" + std::to_string(i);
        fileNames.push_back(fileName);
        
        // Add cache entry normally first
        AddCacheEntry(fileName, fileSize, 0, "testGroup");
        
        // Calculate hash bucket for this filename
        size_t bucket = std::hash<std::string>{}(fileName) % NUM_MAPS;
        LOG_INFO("Adding file '{}' to bucket {}", fileName, bucket);
        auto& cacheMap = CacheManager::GetInstance().m_vCacheInfoMap[bucket];
        auto iter = cacheMap.find(fileName);
        ASSERT_NE(iter, cacheMap.end()) << "File should exist in cache";
        
        // Set access time to different hours:
        // File 0: 4 hours ago (oldest) - will be in earliest bucket
        // File 1: 3 hours ago 
        // File 2: 2 hours ago
        // File 3: 1 hour ago (newest) - will be in latest bucket
        iter->second.m_accessTimestamp = baseTime - ((3 - i) * 3600) + 20;
    }
    

    // Now test the dynamic eviction threshold calculation iteration logic
    time_t threshold = CacheManager::GetInstance().CalculateDynamicEvictionThreshold();
    
    // Expected algorithm behavior:
    // Current: 800MB, Target: 40% of 1GB=400MB, Need to evict: 400MB
    // Bucket iteration should accumulate: 200MB + 200MB = 400MB (first 2 buckets)
    // Should stop at second bucket and set threshold = secondBucketTime + 3600
    
    ASSERT_GT(threshold, 0) << "Algorithm should return valid threshold";
    
    // Validate threshold is within reasonable range relative to our test data
    time_t minExpectedThreshold = baseTime - 2 * 3600; //
    time_t maxExpectedThreshold = baseTime - 3600; //
    
    ASSERT_GE(threshold, minExpectedThreshold) << "Threshold should not be older than min";
    ASSERT_LE(threshold, maxExpectedThreshold) << "Threshold should not be newer than max";
    
   
    //sleep for one eviction pass to allow eviction to happen
    std::this_thread::sleep_for(std::chrono::seconds(3));    
    // Validate that the first two files were evicted
    ValidateCacheEntryNotExists(fileNames[0]);
    ValidateCacheEntryNotExists(fileNames[1]);
    ValidateCacheEntry(fileNames[2], fileSize, 0, "testGroup");
    ValidateCacheEntry(fileNames[3], fileSize, 0, "testGroup");
    ASSERT_EQ(CacheManager::GetInstance().m_totalCachedSize.load(), 2 * fileSize);
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheManagerTest, IsKVCacheEntryMatchesClientAndLegacyTags)
{
    google::protobuf::Map<std::string, std::string> metadata;
    EXPECT_FALSE(CacheManager::IsKVCacheEntry(metadata));

    metadata["other"] = "1";
    EXPECT_FALSE(CacheManager::IsKVCacheEntry(metadata));

    metadata["dacs_kv"] = "1";
    EXPECT_TRUE(CacheManager::IsKVCacheEntry(metadata));

    google::protobuf::Map<std::string, std::string> legacyMetadata;
    legacyMetadata["tachyonkv"] = "1";
    EXPECT_TRUE(CacheManager::IsKVCacheEntry(legacyMetadata));
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(CacheManagerTest, EvictionUnregistersKVEntriesFromLocator)
{
    // Hold the eviction mutex for the whole test so the background eviction pass can't run.
    std::lock_guard<std::mutex> lock(CacheManager::GetInstance().m_mutexEviction);
    RESET_CACHE_MANAGER();
    kvclient::ResetServerLocatorCacheForTest(
        std::make_unique<kvclient::FlatMapIndex>(), /*nNodeId=*/0, /*nNumNodes=*/1);
    auto& locator = kvclient::GetServerLocatorCache();

    const uint64_t nFileSize = 4096;
    const uint64_t nNowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    const std::vector<std::pair<std::string, std::string>> entries = {
        {"/kv/client-tag", "dacs_kv"},
        {"/kv/legacy-tag", "tachyonkv"},
        {"/kv/untagged", ""},
    };
    for (const auto& [strName, strTag] : entries)
    {
        ASSERT_TRUE(CacheManager::GetInstance().InitCacheInfo(strName, true /* ignore lock */, nFileSize));
        google::protobuf::Map<std::string, std::string> metadata;
        if (!strTag.empty())
        {
            metadata[strTag] = "1";
        }
        ASSERT_TRUE(CacheManager::GetInstance().UpdateCacheInfo(strName, nFileSize, metadata, 0, ""));
        locator.Register(strName, ContentReplica(locator.GetNodeId(), nNowMs + 3600 * 1000, nNowMs, nFileSize));
        ASSERT_TRUE(locator.Lookup(strName).has_value());
    }

    // Evict everything accessed up to a minute from now, i.e. all three entries.
    CacheManager::GetInstance().EvictByAccessTime(time(nullptr) + 60);

    for (const auto& [strName, strTag] : entries)
    {
        ValidateCacheEntryNotExists(strName);
    }
    EXPECT_FALSE(locator.Lookup("/kv/client-tag").has_value());
    EXPECT_FALSE(locator.Lookup("/kv/legacy-tag").has_value());
    // Untagged entries are not KV entries, so eviction leaves their locator entries alone.
    EXPECT_TRUE(locator.Lookup("/kv/untagged").has_value());
}
//---------------------------------------------------------------------------------------------------------------------
