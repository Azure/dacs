/// Copyright (c) Microsoft Corporation.
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

#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "cache.pb.h"
#include "snapshot.pb.h"
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <list>
#include <queue>
#include <gtest/gtest.h>
//---------------------------------------------------------------------------------------------------------------------
class CacheManagerTest;
//---------------------------------------------------------------------------------------------------------------------
class CacheManager
{
    // Friends for testing
    FRIEND_TEST(CacheManagerTest, TakeAndLoadSnapshot);
    FRIEND_TEST(CacheManagerTest, RembemberPendingGroupIdDeletionAfterSnapshotLoad);
    FRIEND_TEST(CacheManagerTest, TestParseTimeIntervalString);
    FRIEND_TEST(CacheManagerTest, CleanupWorkerStartsWithConfiguredInterval);
    FRIEND_TEST(CacheManagerTest, TestEviction);
    FRIEND_TEST(CacheManagerTest, TestDynamicEviction);

    public:
        static CacheManager& GetInstance();

        enum class CacheStatus
        {
            NOTFOUND, INITIATED, STARTED, COMPLETED
        };

        struct CacheInfo
        {
            CacheInfo(uint64_t fileSize, const ::google::protobuf::Map<std::string, std::string>& metadata,
                CacheStatus status, time_t expiryTime, const std::string& groupId)
                : m_fileSize(fileSize)
                , m_metadata(metadata)
                , m_status(status)
                , m_expiryTime(expiryTime)
                , m_groupId(groupId)
                , m_initTimestamp(time(NULL))
                , m_accessTimestamp(m_initTimestamp)
                {}
            CacheInfo(CacheStatus status)
                : m_fileSize(0)
                , m_status(status)
                , m_expiryTime(0)
                , m_groupId("")
                , m_initTimestamp(time(NULL))
                , m_accessTimestamp(m_initTimestamp)
                {}

            uint64_t m_fileSize;
            ::google::protobuf::Map<std::string, std::string> m_metadata;
            CacheStatus m_status;
            time_t m_expiryTime;
            std::string m_groupId;
            // Declaration order lets us reuse initTimestamp for accessTimestamp
            // in initializer list
            time_t m_initTimestamp;
            time_t m_accessTimestamp;
        };

        // Fetch cached info and initialize (to coordinate multiple updates from clients) if it does not exist
        CacheInfo GetAndInitCacheInfo(
            /*[in] */ const std::string& fileName);

        // Fetch cached attributes
        CacheInfo GetCacheInfo(
            /*[in] */ const std::string& fileName);

        // Initialize cache info for file to indicate start of an cache upload
        // Set ignoreLock to false to ensure upload is started after the file is locked, this mechanism is used
        // to ensure only one client attempts to upload a file
        bool InitCacheInfo(
            /*[in] */ const std::string& fileName,
            /*[in] */ bool ignoreLock,
            /*[in] */ uint64_t fileSize);

        // Remove/reset cache entry in case of incomplete uploads to allow for next clean uploads
        bool ResetCacheInfo(
            /*[in] */ const std::string& fileName,
            /*[in] */ uint64_t fileSize);

        // Update cache info after upload file is successful
        bool UpdateCacheInfo(
            /*[in] */ const std::string& fileName,
            /*[in] */ uint64_t fileSize,
            /*[in] */ const ::google::protobuf::Map<std::string, std::string>& metadata,
            /*[in] */ uint32_t cacheExpirySeconds,
            /*[in] */ const std::string& groupId);

        // Delete all cache files matching the group-id.
        // This is asynchronous and actual file deletion happens in the background after this call returns.
        bool DeleteCacheEntryByGroupID(
            /*[in] */ const std::string& groupId);

        bool DeleteCacheEntryByLastAccessTime(
            /*[in] */ const std::string& strLastAccessTime);

        void SetCacheSizeLimit(
            /*[in] */ const uint64_t limitCacheSizeBytes);

        // Starts the cleanup worker after applying the startup-only TTL window configuration.
        void StartCacheCleanupWorker(
            /*[in] */ const uint32_t cacheTTLWindowSizeSeconds);

        std::shared_ptr<snapshot::Snapshot> TakeSnapshot() const;

        void InitializeFromSnapshot(
            /*[in] */ const snapshot::Snapshot& snapshot);
        
        void ScanForSnapshotConsistency(
            /*[in] */ const snapshot::Snapshot& snapshot);

        inline std::size_t GetCacheServerSize() const
        {
            return (std::size_t)m_totalCachedSize.load();
        }

        std::size_t GetCacheServerSizeLimit() const
        {
            return (std::size_t)m_cacheSizeLimitBytes.load();
        }

        // Reset the cache size counter to 0. Used as a recovery mechanism when
        // unsigned underflow is detected (counter wrapped to a huge value).
        void ResetCacheSizeCounter()
        {
            m_totalCachedSize.store(0);
        }

        inline double GetCacheServerUtilizationPercentage() const
        {
            return static_cast<double>(m_totalCachedSize.load()) * 100.0
                / static_cast<double>(m_cacheSizeLimitBytes.load());
        }

        bool GetLastAccessTimeDistribution(uint64_t distributionBucketIntervalInSeconds,
                                           ::google::protobuf::Map<int64_t, uint64_t>& lastAccessTimeDistributionMap) const;

      private:
        CacheManager();
        ~CacheManager();

        // Calculate the bucketized expiry time given cache expiry seconds
        time_t GetExpiryTimeBucket(
            /*[in] */ uint32_t cacheExpirySeconds) const;

        // Determine if eviction feature flag is turned on
        // Read the env var to set the eviction last access threshold.
        // Read the env var to set the eviction pass frequency.
        // Value of the envvar must be a string of the format
        // [[0-9]+[s|m|h|d]]+ where s=seconds, m=minutes, h=hours, d=days
        // Example: CACHESERVER_EVICTION_LAST_ACCESS_THRESHOLD="30d12h41m29s"
        void InitializeEviction();

        static uint64_t ParseTimeIntervalString(const char* timeInterval);

        static time_t ParseAndConvertToTime(const std::string& timeString);

        // Delete file path from local disk
        void DeleteFile(
            /*[in] */ const std::string& filename);

        void CacheCleanupWorker();

        void EvictionWorker();

        void EvictByAccessTime(time_t nLastAccessTime);

        // Calculate dynamic eviction threshold based on cache size and access time distribution
        time_t CalculateDynamicEvictionThreshold();

        // Setting some default expiry time to ensure one client doesn't permanently lock a cache key
        static constexpr uint32_t CACHE_INIT_EXPIRY_SECONDS = 60;

        std::atomic_uint_fast64_t m_cacheSizeLimitBytes;
        std::atomic_uint_fast64_t m_totalCachedSize;

        // Bucketized Cache info map
        std::vector<std::unordered_map<std::string, CacheInfo>> m_vCacheInfoMap;

        // mutexes for cached buckets
        mutable std::vector<std::mutex> m_vCacheInfoMapMutex;

        // keys stored in buckets based on its expiry time and sorted by time ascending.
        // The buckets are created using the same key as cache info map and uses same mutexes for synchronization
        std::vector<std::map<time_t, std::list<std::string>>> m_vCacheTTLMap;

        // The ttl bucket window size. All files with expiry time failing within this bucket will expire together
        std::atomic_uint m_ttlWindowSizeSeconds;

        // Eviction related members
        bool m_bEvictionEnabled;
        bool m_bDynamicEvictionEnabled;
        // Parameter used by the eviction thread to determine which items to evict.
        // All items last accessed within this number of seconds ago are considered active.
        // i.e lastAccessTime < m_nEvictionLastAccessThresholdSeconds => Active
        uint64_t m_nEvictionLastAccessThresholdSeconds;
        // Parameter used by the eviction thread to determine how often to run.
        uint32_t m_nEvictionPassFrequencySeconds;
        // Parameter used by the eviction thread to determine the cache size threshold for eviction.
        double m_dEvictionSizeThreshold;
        // Parameter used by the dynamic eviction algorithm to determine target utilization after eviction.
        // Target utilization percentage (e.g., 70 means target 70% utilization)
        double m_dEvictionSizeTarget;
        // Bucket granularity (in seconds) used by the dynamic eviction algorithm to bucketize the
        // last-access-time distribution and to round up the computed eviction cutoff. Historically
        // hardcoded to 3600 (1 hour); now configurable via CACHESERVER_EVICTION_BUCKET_INTERVAL.
        uint64_t m_nEvictionBucketIntervalSeconds;
        std::atomic_bool m_bStopEvictionThread;
        std::mutex m_mutexEviction; // really only used by the wait_for
        std::condition_variable m_cvEviction;
        std::unique_ptr<std::thread> m_evictionThread;

        // keys stored in buckets based on its group-id
        std::vector<std::unordered_map<std::string, std::list<std::string>>> m_vCacheGroupIDMap;

        // mutexes for the group-id buckets
        mutable std::vector<std::mutex> m_vCacheGroupIDMapMutex;

        // Background thread processing all deletes
        std::unique_ptr<std::thread> m_cleanupThread;

        // Flag to stop the cleanup thread
        std::atomic_bool m_stopCleanupThread;

        // Queue to mark group-ids for deletion
        std::queue<std::string> m_groupIdQueue;

        // Mutex to sychronize the deletion queue
        mutable std::mutex m_mutexQueue;

        // CV for waking up the cleanup thread and manage cleanup timer
        std::condition_variable m_cvQueue;
};
//---------------------------------------------------------------------------------------------------------------------