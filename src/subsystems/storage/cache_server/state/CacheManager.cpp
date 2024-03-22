///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheManager.h"
#include "Constants.h"
#include "Utils.h"
#include "Logger.h"
#include <chrono>
#include <dirent.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace snapshot;
using namespace std;
using namespace std::chrono_literals;
//---------------------------------------------------------------------------------------------------------------------
CacheManager::CacheManager()
: m_vCacheInfoMap(NUM_MAPS),
  m_vCacheInfoMapMutex(NUM_MAPS),
  m_vCacheTTLMap(NUM_MAPS),
  m_vCacheGroupIDMap(NUM_MAPS),
  m_vCacheGroupIDMapMutex(NUM_MAPS),
  m_stopCleanupThread(false),
  m_bStopEvictionThread(false),
  m_bEvictionEnabled(false),
  m_ttlWindowSizeSeconds(DefaultCacheTTLWindowSizeSeconds),
  m_cacheSizeLimitBytes(DefaultCacheSizeLimitBytes),
  m_totalCachedSize(0)
{
    InitializeEviction();

    m_cleanupThread = make_unique<thread>([this]() {
        this->CacheCleanupWorker();
    });

    if (m_bEvictionEnabled)
    {
        m_evictionThread = make_unique<thread>([this]() {
            this->EvictionWorker();
        });
    }
}
//---------------------------------------------------------------------------------------------------------------------
CacheManager::~CacheManager()
{
    m_stopCleanupThread = true;
    m_cvQueue.notify_all();
    m_cleanupThread->join();

    if (m_bEvictionEnabled)
    {
        m_bStopEvictionThread.store(true);
        m_cvEviction.notify_all();
        m_evictionThread->join();
    }
}
//---------------------------------------------------------------------------------------------------------------------
CacheManager& CacheManager::GetInstance()
{
    static CacheManager s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
void CacheManager::SetCacheSizeLimit(const uint64_t limitCacheSizeBytes)
{
    m_cacheSizeLimitBytes = limitCacheSizeBytes;
}
//---------------------------------------------------------------------------------------------------------------------
void CacheManager::SetCacheTTLWindowSize(const uint32_t cacheTTLWindowSizeSeconds)
{
    m_ttlWindowSizeSeconds = cacheTTLWindowSizeSeconds;
}
//---------------------------------------------------------------------------------------------------------------------
CacheManager::CacheInfo CacheManager::GetAndInitCacheInfo(const std::string& fileName)
{
    CacheInfo prevCacheStatus(CacheStatus::NOTFOUND);
    auto index = GetHashBucket(fileName);

    lock_guard lockGuard(m_vCacheInfoMapMutex[index]);
    auto iter = m_vCacheInfoMap[index].find(fileName);
    if (iter == m_vCacheInfoMap[index].end())
    {
        m_vCacheInfoMap[index].insert(make_pair(fileName,
            CacheInfo(0, ::google::protobuf::Map<std::string, std::string>(), CacheStatus::INITIATED, 0, "")));
    }
    else
    {
        // Update access time
        time_t now = time(NULL);
        iter->second.m_accessTimestamp = now;

        // If the previous init happened a while ago, we can reset it for new uploads to proceed
        if (iter->second.m_status == CacheStatus::INITIATED &&
            iter->second.m_initTimestamp + CACHE_INIT_EXPIRY_SECONDS < time(NULL))
        {
            iter->second.m_initTimestamp = now;
            return prevCacheStatus;
        }

        prevCacheStatus = iter->second;
    }

    return prevCacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
CacheManager::CacheInfo CacheManager::GetCacheInfo(const std::string& fileName)
{
    CacheInfo prevCacheStatus(CacheStatus::NOTFOUND);
    auto index = GetHashBucket(fileName);

    lock_guard lockGuard(m_vCacheInfoMapMutex[index]);
    auto iter = m_vCacheInfoMap[index].find(fileName);
    if (iter != m_vCacheInfoMap[index].end())
    {
        iter->second.m_accessTimestamp = time(NULL);
        prevCacheStatus = iter->second;
    }

    return prevCacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
bool CacheManager::InitCacheInfo(const std::string& fileName, bool ignoreLock, uint64_t fileSize)
{
    if (fileSize > 0 && m_totalCachedSize.fetch_add(fileSize) > m_cacheSizeLimitBytes)
    {
        LOG_RARELY(30, WARN, "TotalCachedSize: {}, exceeds limit: {}", m_totalCachedSize, m_cacheSizeLimitBytes);
        m_totalCachedSize.fetch_sub(fileSize);
        return false;
    }

    auto index = GetHashBucket(fileName);
    unique_lock lockCache(m_vCacheInfoMapMutex[index]);
    auto iter = m_vCacheInfoMap[index].find(fileName);
    if (iter != m_vCacheInfoMap[index].end())
    {
        if (iter->second.m_status == CacheStatus::COMPLETED)
        {
            // Some other upload for same file has already succeeded, ignore this one.
            LOG_WARN("File already uploaded by another thread: {}", fileName);
            m_totalCachedSize.fetch_sub(fileSize);
            // Update access time
            iter->second.m_accessTimestamp = time(NULL);
            return false;
        }
        iter->second = CacheInfo(CacheStatus::STARTED);
    }
    else
    {
        if (ignoreLock)
        {
            m_vCacheInfoMap[index].insert(make_pair(fileName, CacheInfo(CacheStatus::STARTED)));
        }
        else
        {
            LOG_WARN("Cache not initiated for file: {}", fileName);
            m_totalCachedSize.fetch_sub(fileSize);
            return false;
        }
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool CacheManager::ResetCacheInfo(const std::string& fileName, uint64_t fileSize)
{
    m_totalCachedSize.fetch_sub(fileSize);

    auto index = GetHashBucket(fileName);
    unique_lock lockCache(m_vCacheInfoMapMutex[index]);
    auto iter = m_vCacheInfoMap[index].find(fileName);
    if (iter != m_vCacheInfoMap[index].end())
    {
        if (iter->second.m_status == CacheStatus::COMPLETED)
        {
            // Clear TTLMap entry
            if (iter->second.m_expiryTime != 0)
            {
                auto ttlMapIter = m_vCacheTTLMap[index].find(iter->second.m_expiryTime);
                if (ttlMapIter != m_vCacheTTLMap[index].end())
                {
                    ttlMapIter->second.remove(iter->first);
                }
            }

            // Clear groupIdMap entry
            if (!iter->second.m_groupId.empty())
            {
                auto groupIndex = GetHashBucket(iter->second.m_groupId);
                lock_guard groupIdBucketLock(m_vCacheGroupIDMapMutex[groupIndex]);
                auto groupIdMapIter = m_vCacheGroupIDMap[groupIndex].find(iter->second.m_groupId);
                if (groupIdMapIter != m_vCacheGroupIDMap[groupIndex].end())
                {
                    groupIdMapIter->second.remove(iter->first);
                }
            }
        }

        m_vCacheInfoMap[index].erase(iter);
    }
    
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool CacheManager::UpdateCacheInfo(const std::string& fileName, uint64_t fileSize, 
    const ::google::protobuf::Map<std::string, std::string>& metadata, uint32_t cacheExpirySeconds,
    const string& groupId)
{
    if (cacheExpirySeconds > 0 && !groupId.empty())
    {
        LOG_WARN("Both expiry and groupid should not be set, ignoring expiry. filename: {}, expiry: {}, groupid: {}",
            fileName, cacheExpirySeconds, groupId);
        cacheExpirySeconds = 0;
    }

    time_t expiryTimeBucket = GetExpiryTimeBucket(cacheExpirySeconds);

    auto index = GetHashBucket(fileName);
    unique_lock lockCache(m_vCacheInfoMapMutex[index]);
    auto iter = m_vCacheInfoMap[index].find(fileName);
    if (iter != m_vCacheInfoMap[index].end())
    {
        if (iter->second.m_status == CacheStatus::COMPLETED)
        {
            // Some other upload for same file has already succeeded, ignore this one.
            LOG_WARN("File already uploaded by another thread: {}", fileName);
            m_totalCachedSize.fetch_sub(fileSize);
            iter->second.m_accessTimestamp = time(NULL);
            return false;
        }

        iter->second = CacheInfo(fileSize, metadata, CacheStatus::COMPLETED, expiryTimeBucket, groupId);
    }
    else
    {
        m_vCacheInfoMap[index].insert(make_pair(
            fileName,
            CacheInfo(fileSize, metadata, CacheStatus::COMPLETED, expiryTimeBucket, groupId)));
    }

    if (expiryTimeBucket > 0)
    {
        m_vCacheTTLMap[index][expiryTimeBucket].emplace_back(fileName);
        return true;
    }
    lockCache.unlock();

    if (!groupId.empty())
    {
        auto groupIndex = GetHashBucket(groupId);
        lock_guard groupLockGuard(m_vCacheGroupIDMapMutex[groupIndex]);
        m_vCacheGroupIDMap[groupIndex][groupId].emplace_back(fileName);
    }
   
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
time_t CacheManager::GetExpiryTimeBucket(uint32_t cacheExpirySeconds) const
{
    time_t expiryTimeBucket = 0;
    if (cacheExpirySeconds > 0)
    {
        time_t now = time(NULL);

        // Treat very large expiry values as no expiry, also avoids integer overflow
        if (cacheExpirySeconds >= (UINT32_MAX - now - m_ttlWindowSizeSeconds))
        {
            return 0;
        }

        expiryTimeBucket = now + cacheExpirySeconds;
        expiryTimeBucket = expiryTimeBucket - (expiryTimeBucket % m_ttlWindowSizeSeconds) + m_ttlWindowSizeSeconds;
    }

    return expiryTimeBucket;
}
//---------------------------------------------------------------------------------------------------------------------
bool CacheManager::DeleteCacheEntryByGroupID(const std::string& groupId)
{
    if (groupId.empty())
    {
        LOG_WARN("groupid is empty");
        return false;
    }

    // Add groupid entry to the delete queue
    std::lock_guard lockGuard(m_mutexQueue);
    m_groupIdQueue.push(groupId);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
void CacheManager::DeleteFile(const std::string& filename)
{
    const string filepath = GetCachePath(filename);
    if (remove(filepath.c_str()) == 0)
    {
        LOG_DEBUG("Deleted filename: {}, path: {}", filename, filepath);
    }
    else
    {
        // errno = 2 (file not found) is expected for 0 sized files as we don't create files on disk for them.
        // We can avoid this but requires extra processing for something which is not common. 
        // Will keep this log as WARN to understand how often this happens.
        LOG_WARN("Failed to delete filename: {}, path: {}, errno: {}:{}", filename, filepath, errno, strerror(errno));
    }
}
//---------------------------------------------------------------------------------------------------------------------
shared_ptr<Snapshot> CacheManager::TakeSnapshot() const
{
    LOG_INFO("Taking a snapshot");
    TIMER_START(takeSnapshot);

    shared_ptr<Snapshot> snapshot = make_shared<Snapshot>();
    snapshot->set_version(SNAPSHOT_VERSION);
    snapshot->set_hostname(GetServerHostName());
    int nEntries = 0;

    for (int bucket = 0; bucket < NUM_MAPS; bucket++)
    {
        auto snapshotBucket = snapshot->add_buckets();
        lock_guard lock(m_vCacheInfoMapMutex[bucket]);
        for (auto iter = m_vCacheInfoMap[bucket].begin(); iter != m_vCacheInfoMap[bucket].end(); iter++)
        {
            if (iter->second.m_status != CacheStatus::COMPLETED)
            {
                continue; // Only considering completed uploads for snapshot.
            }

            nEntries++;
            auto fileEntry = snapshotBucket->add_entries();
            fileEntry->set_filename(iter->first);
            fileEntry->set_filesize(iter->second.m_fileSize);
            for (auto metadataIter = iter->second.m_metadata.begin(); metadataIter != iter->second.m_metadata.end(); metadataIter++)
            {
                (*fileEntry->mutable_metadata())[metadataIter->first] = metadataIter->second;
            }

            fileEntry->set_expirytimestamp(iter->second.m_expiryTime);
            fileEntry->set_groupid(iter->second.m_groupId);
            fileEntry->set_accesstimestamp(iter->second.m_accessTimestamp);
        }
    }

    std::unique_lock<std::mutex> groupIDLock(m_mutexQueue);
    std::queue<string> groupIDQueueCopy = m_groupIdQueue;
    groupIDLock.unlock();
    while (!groupIDQueueCopy.empty())
    {
        const string& groupId = groupIDQueueCopy.front();
        groupIDQueueCopy.pop();

        snapshot->add_todeletegroups(groupId);
    }

    // Set save timestamp at the end, this is to ensure that 
    // savetimestamp > accesstimestamp for all files in snapshot
    snapshot->set_savetimestamp(time(NULL));
    LOG_INFO("Completed taking a snapshot. num entries: {}. toDeleteGroups: {} Snapshot size: {}",
      nEntries,
      snapshot->todeletegroups_size(),
      snapshot->ByteSize());
    TIMER_STOP_AND_LOG(takeSnapshot);
    return snapshot;
}
//---------------------------------------------------------------------------------------------------------------------
void CacheManager::InitializeFromSnapshot(const Snapshot& snapshot)
{
    LOG_INFO("Initializing cache from snapshot");
    TIMER_START(initFromSnapshot);

    int nEntries = 0;
    time_t now = time(NULL);
    unordered_map<string, list<string>> groupIDMapEntries;
    for (int bucket = 0; bucket < snapshot.buckets_size(); bucket++)
    {
        lock_guard lock(m_vCacheInfoMapMutex[bucket]);
        const SnapshotBucket& snapshotBucket = snapshot.buckets(bucket);
        for (int fileIndex = 0; fileIndex < snapshotBucket.entries_size(); fileIndex++)
        {
            if (snapshotBucket.entries(fileIndex).expirytimestamp() > 0 &&
                snapshotBucket.entries(fileIndex).expirytimestamp() < now)
            {
                LOG_INFO("Skipping expired file from snapshot. bucket: {}, filename: {}",
                    bucket,
                    snapshotBucket.entries(fileIndex).filename());
                continue;
            }
            else if (snapshotBucket.entries(fileIndex).accesstimestamp() > 0 &&
                    (snapshot.savetimestamp() - snapshotBucket.entries(fileIndex).accesstimestamp()) >= m_nEvictionLastAccessThresholdSeconds)
            {
                LOG_INFO("Skipping stale file from snapshot. bucket: {}, filename: {} Access timestamp: {} Snapshot savetimestamp: {} last access threshold: {} seconds ago",
                    bucket,
                    snapshotBucket.entries(fileIndex).filename(),
                    snapshotBucket.entries(fileIndex).accesstimestamp(),
                    snapshot.savetimestamp(),
                    m_nEvictionLastAccessThresholdSeconds);
                continue;
            }

            nEntries++;
            const auto& fileEntry = snapshotBucket.entries(fileIndex);

           auto insertedItemIter = m_vCacheInfoMap[bucket].insert(make_pair(fileEntry.filename(),
                CacheInfo(
                    fileEntry.filesize(),
                    fileEntry.metadata(),
                    CacheStatus::COMPLETED,
                    fileEntry.expirytimestamp(),
                    fileEntry.groupid()))).first;

            time_t accessTimestamp = fileEntry.accesstimestamp();
            // Backwards compatibility with older snapshots
            // If access timestamp was not available, then all
            // files in the snapshot are given the current time as access time
            // This is not ideal, but better than giving them all 0 as access time
            if (accessTimestamp == 0)
            {
                accessTimestamp = now;
            }

            insertedItemIter->second.m_accessTimestamp = accessTimestamp;

            if (fileEntry.expirytimestamp() > 0)
            {
                m_vCacheTTLMap[bucket][fileEntry.expirytimestamp()].emplace_back(fileEntry.filename());
            }

            if (!fileEntry.groupid().empty())
            {
                groupIDMapEntries[fileEntry.groupid()].emplace_back(fileEntry.filename());
            }

            m_totalCachedSize.fetch_add(fileEntry.filesize());
        }

        LOG_INFO("Loaded snapshot entries into cache and TTL map for bucket: {}, num bucket entries: {}",
            bucket,
            m_vCacheInfoMap[bucket].size());
    }

    LOG_INFO("Loaded snapshot entries into cache and TTL map. Now Loading into groupID map. num entries: {}, cacheSize: {}",
        nEntries,
        m_totalCachedSize.load());
    for (auto& groupIDMapEntry : groupIDMapEntries)
    {
        auto groupIndex = GetHashBucket(groupIDMapEntry.first);
        lock_guard groupLockGuard(m_vCacheGroupIDMapMutex[groupIndex]);
        m_vCacheGroupIDMap[groupIndex][groupIDMapEntry.first].splice(
            m_vCacheGroupIDMap[groupIndex][groupIDMapEntry.first].end(), groupIDMapEntry.second);
    }

    if (snapshot.todeletegroups_size() > 0)
    {
        std::unique_lock<std::mutex> groupIDLock(m_mutexQueue);
        for (int i = 0; i < snapshot.todeletegroups_size(); i++)
        {
            m_groupIdQueue.push(snapshot.todeletegroups(i));
        }

        LOG_INFO("Loaded snapshot toDeleteGroups into queue. num groups: {}", snapshot.todeletegroups_size());
    }
  
    LOG_INFO("Completed initializing cache from snapshot. num entries: {}", nEntries);
    TIMER_STOP_AND_LOG(initFromSnapshot);
}
//---------------------------------------------------------------------------------------------------------------------
void CacheManager::ScanForSnapshotConsistency(const Snapshot& snapshot)
{
    LOG_INFO("Started scanning for snapshot consistency");
    TIMER_START(scanForSnapshotConsistency);

    for (int bucket = 0; bucket < snapshot.buckets_size(); bucket++)
    {
        LOG_INFO("Scanning for snapshot consistency bucket: {} started", bucket);

        // Do a readdir to get all files in the cache folder for this bucket on disk
        unordered_set<string> filesOnDisk;
        const std::string& cacheFolderPath = GetCacheFolderPath() + "/" + to_string(bucket);
        DIR* dir = opendir(cacheFolderPath.c_str());
        STORAGE_ASSERT_NOTHROW(dir != NULL, "Failed to open cache folder: {}", cacheFolderPath);

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            {
                STORAGE_ASSERT_NOTHROW(
                    entry->d_type == DT_REG,
                    "Unexpected entry type in cache folder: {} name: {}", entry->d_type, entry->d_name);
                filesOnDisk.insert(entry->d_name);
            }
        }

        closedir(dir);
        LOG_INFO("Found {} files on disk for bucket: {}", filesOnDisk.size(), bucket);

        // Get all non-zero size files for this bucket in the snapshot
        unordered_set<string> filesInSnapshot;
        const SnapshotBucket& snapshotBucket = snapshot.buckets(bucket);
        for (int fileIndex = 0; fileIndex < snapshotBucket.entries_size(); fileIndex++)
        {
            if (snapshotBucket.entries(fileIndex).filesize() > 0)
            {
                filesInSnapshot.insert(FileNameEncode(snapshotBucket.entries(fileIndex).filename()));
            }
        }

        LOG_INFO("Found {} files in snapshot for bucket: {}", filesInSnapshot.size(), bucket);

        // Delete files on disk not found in snapshot
        int nDeletedFromDisk = 0;
        for (auto& fileOnDisk : filesOnDisk)
        {
            if (filesInSnapshot.find(fileOnDisk) == filesInSnapshot.end())
            {
                LOG_INFO("File on disk not found in snapshot. bucket: {}, filename: {}", bucket, fileOnDisk);
                nDeletedFromDisk++;
                int ret = remove((cacheFolderPath + "/" + fileOnDisk).c_str());
                if (ret != 0)
                {
                    LOG_WARN("Failed to delete file on disk. bucket: {}, filename: {}, errno: {}:{}",
                        bucket,
                        fileOnDisk,
                        errno,
                        strerror(errno));
                }
            }
        }

        LOG_INFO("Deleted {} files on disk not found in snapshot. bucket: {}", nDeletedFromDisk, bucket);

        // Delete from cache index the files present in snapshot but not found on disk
        int nDeletedFromCacheIndex = 0;
        unordered_map<string, unordered_set<string>> groupIDMapEntriesToCleanup;
        std::unique_lock lock(m_vCacheInfoMapMutex[bucket]);
        for (int fileIndex = 0; fileIndex < snapshotBucket.entries_size(); fileIndex++)
        {
            const std::string fileInSnapshot = FileNameEncode(snapshotBucket.entries(fileIndex).filename());
            if (filesOnDisk.find(fileInSnapshot) == filesOnDisk.end())
            {
                LOG_INFO("File in snapshot not found on disk. bucket: {}, filename: {}", bucket, fileInSnapshot);
                nDeletedFromCacheIndex++;

                m_vCacheInfoMap[bucket].erase(fileInSnapshot);
                if (snapshotBucket.entries(fileIndex).expirytimestamp() > 0)
                {
                    m_vCacheTTLMap[bucket][snapshotBucket.entries(fileIndex).expirytimestamp()].remove(fileInSnapshot);
                }

                if (!snapshotBucket.entries(fileIndex).groupid().empty())
                {
                    groupIDMapEntriesToCleanup[snapshotBucket.entries(fileIndex).groupid()].insert(fileInSnapshot);
                }

                m_totalCachedSize.fetch_sub(snapshotBucket.entries(fileIndex).filesize());
            }
        }

        lock.unlock();
        LOG_INFO("Deleted {} files from cache and TTL index not found on disk. bucket: {}", nDeletedFromCacheIndex, bucket);

        int nDeletedFromGroupIDIndex = 0;
        for (auto& groupIDMapEntryToCleanup : groupIDMapEntriesToCleanup)
        {
            auto groupIndex = GetHashBucket(groupIDMapEntryToCleanup.first);
            lock_guard groupLockGuard(m_vCacheGroupIDMapMutex[groupIndex]);
            if (m_vCacheGroupIDMap[groupIndex].find(groupIDMapEntryToCleanup.first) != m_vCacheGroupIDMap[groupIndex].end())
            {
                auto& groupIdMap = m_vCacheGroupIDMap[groupIndex][groupIDMapEntryToCleanup.first];
                auto iter = groupIdMap.begin();
                while (iter != groupIdMap.end())
                {
                    if (groupIDMapEntryToCleanup.second.find(*iter) != groupIDMapEntryToCleanup.second.end())
                    {
                        nDeletedFromGroupIDIndex++;
                        iter = groupIdMap.erase(iter);
                    }
                    else
                    {
                        iter++;
                    }
                }
            }
        }

        LOG_INFO("Deleted {} files from groupID index not found on disk. bucket: {}", nDeletedFromGroupIDIndex, bucket);
        LOG_INFO("Scanning for snapshot consistency bucket: {} completed", bucket);
    }
  
    LOG_INFO("Completed scanning for snapshot consistency");
    TIMER_STOP_AND_LOG(scanForSnapshotConsistency);
}
//---------------------------------------------------------------------------------------------------------------------
void CacheManager::CacheCleanupWorker()
{
    LOG_INFO("Started CacheCleanupWorker. Sleep timer in seconds: {}", m_ttlWindowSizeSeconds);
    while (!m_stopCleanupThread)
    {
        // wait until timeout or shutdown notification
        std::unique_lock<std::mutex> groupIDLock(m_mutexQueue);
        if (m_cvQueue.wait_for(
            groupIDLock,
            m_ttlWindowSizeSeconds.load() * 1s,
            [this]
            {
                return (this->m_stopCleanupThread == true);
            }) == true)
        {
            break;
        }

        // unlock the mutex for now. We will delete groupIds from queue later after cleaning them up from m_vCacheInfoMap
        // so that they get included in snapshot in case server crashes during metadata cleanup.
        queue<string> groupIDsToCleanup = m_groupIdQueue;
        size_t nGroupIdsToCleanup = m_groupIdQueue.size();
        groupIDLock.unlock();

        // Get all the keys to be deleted
        vector<list<string>> filesToDelete(NUM_MAPS);

        // Get all expired keys
        time_t currentTime = time(NULL);
        for (int bucket = 0; bucket < NUM_MAPS; bucket++)
        {
            lock_guard lock(m_vCacheInfoMapMutex[bucket]);
            auto iter = m_vCacheTTLMap[bucket].begin();            
            while (iter != m_vCacheTTLMap[bucket].end())
            {
                if (iter->first <= currentTime)
                {
                    filesToDelete[bucket].splice(filesToDelete[bucket].end(), iter->second);
                    iter = m_vCacheTTLMap[bucket].erase(iter);
                }
                else
                {
                    break;
                }
            }
        }

        // Now include all keys from the groupid map
        while (!groupIDsToCleanup.empty())
        {
            string groupId = groupIDsToCleanup.front();
            groupIDsToCleanup.pop();

            int bucket = GetHashBucket(groupId);
            lock_guard lock(m_vCacheGroupIDMapMutex[bucket]);
            auto iter = m_vCacheGroupIDMap[bucket].find(groupId);
            if (iter != m_vCacheGroupIDMap[bucket].end())
            {
                for (auto filename : iter->second)
                {
                    filesToDelete[GetHashBucket(filename)].emplace_back(filename);
                }
                m_vCacheGroupIDMap[bucket].erase(iter);
            }
        }

        // total size of files that will get deleted
        uint64_t deleteFileSize = 0;
        int numFilesDeleted = 0;

        // remove the keys from the main cache map
        for (int bucket = 0; bucket < NUM_MAPS; bucket++)
        {
            lock_guard lock(m_vCacheInfoMapMutex[bucket]);
            for (auto filename : filesToDelete[bucket])
            {
                auto iter = m_vCacheInfoMap[bucket].find(filename);
                if (iter != m_vCacheInfoMap[bucket].end())
                {
                    numFilesDeleted++;
                    deleteFileSize += iter->second.m_fileSize;
                    m_vCacheInfoMap[bucket].erase(iter);
                }
            }
        }

        // Cleanup the deleted groupIds from the queue
        groupIDLock.lock();
        while (nGroupIdsToCleanup > 0)
        {
            nGroupIdsToCleanup--;
            m_groupIdQueue.pop();
        }
        groupIDLock.unlock();

        // updating here before actual deletes to avoid failing new uploads
        m_totalCachedSize.fetch_sub(deleteFileSize);
        LOG_INFO("Delete files. num files: {}, size: {}", numFilesDeleted, deleteFileSize);

        // Now start deleting the files from disk
        for (int bucket = 0; bucket < NUM_MAPS; bucket++)
        {
            for (auto filename : filesToDelete[bucket])
            {
                // In the download path, we gracefully handle the case where cache entry
                // is present in the map, but absent on disk
                DeleteFile(filename);
            }
        }
    }

    LOG_INFO("Shutdown CacheCleanupWorker");
}
//---------------------------------------------------------------------------------------------------------------------
std::size_t CacheManager::GetCacheServerSize() const
{
    return (std::size_t)m_totalCachedSize.load();
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t CacheManager::ParseTimeIntervalString(const char* timeInterval)
{
    if(timeInterval == nullptr)
    {
        LOG_WARN("Expected non-null time interval string");
        return 0;
    }

    // Time interval string in the format [[0-9]+[d|h|m|s]]+
    char* ptr = (char*)timeInterval;
    uint64_t totalSeconds = 0;
    while(*ptr != '\0')
    {
        char* pEndPtr = nullptr;
        uint64_t nValue = std::strtoul(ptr, &pEndPtr, 10);
        if (pEndPtr == ptr || nValue < 0 )
        {
            LOG_WARN("Invalid time interval string: {}", timeInterval);
            return 0;
        }

        // pEndPtr points to the character after the number
        ptr = pEndPtr;
        switch(*ptr)
        {
            case 'd':
                totalSeconds += (nValue * 86400);
                break;
            case 'h':
                totalSeconds += (nValue * 3600);
                break;
            case 'm':
                totalSeconds += (nValue * 60);
                break;
            case 's':
                totalSeconds += nValue;
                break;
            default:
                LOG_WARN("Invalid time interval string: {}", timeInterval);
                return 0;
        }

        ptr++;
    }

    // Not checking for overflow, uint64_t should be large enough
    return totalSeconds;
}
//---------------------------------------------------------------------------------------------------------------------
void CacheManager::InitializeEviction()
{
    char* evictionEnabled = std::getenv("CACHESERVER_EVICTION_ENABLED");
    if (evictionEnabled != NULL && strcasecmp(evictionEnabled, "true") == 0)
    {
        m_bEvictionEnabled = true;
        LOG_INFO("Cache stale items eviction enabled");
    }

    char* evictionLastAccessThreshold = std::getenv("CACHESERVER_EVICTION_LAST_ACCESS_THRESHOLD");
    if (evictionLastAccessThreshold != NULL && strlen(evictionLastAccessThreshold) > 0)
    {
        uint64_t nValue = ParseTimeIntervalString(evictionLastAccessThreshold);
        if (nValue == 0)
        {
            m_nEvictionLastAccessThresholdSeconds = DefaultEvictionLastAccessThresholdSeconds;
            LOG_WARN("Invalid eviction last access threshold set, using default: {}", m_nEvictionLastAccessThresholdSeconds);
        }
        else
        {
            m_nEvictionLastAccessThresholdSeconds = nValue;
            LOG_INFO("Eviction last access threshold seconds: {}", m_nEvictionLastAccessThresholdSeconds);
        }
    }
    else
    {
        m_nEvictionLastAccessThresholdSeconds = DefaultEvictionLastAccessThresholdSeconds;
        LOG_INFO("Eviction last access threshold seconds not set, using default: {}", m_nEvictionLastAccessThresholdSeconds);
    }

    char* evictionPassFrequency = std::getenv("CACHESERVER_EVICTION_PASS_FREQUENCY");
    if (evictionPassFrequency != NULL && strlen(evictionPassFrequency) > 0)
    {
        uint64_t nValue = ParseTimeIntervalString(evictionPassFrequency);
        if (nValue == 0)
        {
            m_nEvictionPassFrequencySeconds = DefaultEvictionPassFrequencySeconds;
            LOG_WARN("Invalid eviction pass frequency set, using default: {}", m_nEvictionPassFrequencySeconds);
        }
        else
        {
            m_nEvictionPassFrequencySeconds = nValue;
            LOG_INFO("Eviction pass frequency seconds: {}", m_nEvictionPassFrequencySeconds);
        }
    }
    else
    {
        m_nEvictionPassFrequencySeconds = DefaultEvictionPassFrequencySeconds;
        LOG_INFO("Eviction pass frequency seconds not set, using default: {}", m_nEvictionPassFrequencySeconds);
    }

    char* evictionSizeThreshold = std::getenv("CACHESERVER_EVICTION_SIZE_THRESHOLD");
    if (evictionSizeThreshold != NULL && strlen(evictionSizeThreshold) > 0)
    {
        double dValue = std::stod(evictionSizeThreshold);
        if (dValue >= 0 && dValue <= 100)
        {
            m_dEvictionSizeThreshold = dValue;
            LOG_INFO("Eviction size threshold: {} Percent", m_dEvictionSizeThreshold);
        }
        else
        {
            m_dEvictionSizeThreshold = DefaultEvictionSizeThresholdPecentage;
            LOG_WARN("Invalid eviction size threshold set, using default: {} Percent", m_dEvictionSizeThreshold);
        }
    }
    else
    {
        m_dEvictionSizeThreshold = DefaultEvictionSizeThresholdPecentage;
        LOG_INFO("Eviction size threshold not set, using default: {} Percent", m_dEvictionSizeThreshold);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void CacheManager::EvictionWorker()
{
    LOG_INFO("Started EvictionWorker. Sleep timer in seconds: {}", m_nEvictionPassFrequencySeconds);
    while (!m_bStopEvictionThread.load())
    {
        // wait until timeout or shutdown notification
        std::unique_lock<std::mutex> evictionLock(m_mutexEviction);
        if (m_cvEviction.wait_for(
            evictionLock,
            m_nEvictionPassFrequencySeconds * 1s,
            [this]
            {
                return (this->m_bStopEvictionThread.load() == true);
            }) == true)
        {
            break;
        }

        //Check if the cache server utilization is below the threshold
        double dCacheServerUtilization = GetCacheServerUtilizationPercentage();
        if(dCacheServerUtilization < m_dEvictionSizeThreshold)
        {
            LOG_INFO("Skipping Eviction, Cache server utilization:{} is below eviction threshold: {}",
                     dCacheServerUtilization,
                     m_dEvictionSizeThreshold);
            continue;
        }

        uint64_t nDeleteFileSize = 0;
        int nNumFilesDeleted = 0;
        vector<string> vecFilesToDelete;

        for(int bucket = 0; bucket < NUM_MAPS; bucket++)
        {
            // Each entry in each map points from GroupId -> list of filenames
            vector<unordered_map<string, list<string>>> vecGroupIdFilesToDelete(NUM_MAPS);
            lock_guard lock(m_vCacheInfoMapMutex[bucket]);
            // Update now timestamp after taking the bucket lock
            // this ensures that now >= accessTimestamp for any item in that bucket
            time_t now = time(NULL);

            // iter points to a [fileName, CacheInfo] entry
            auto iter = m_vCacheInfoMap[bucket].begin();
            while (iter != m_vCacheInfoMap[bucket].end())
            {
                if(iter->second.m_status == CacheStatus::COMPLETED &&
                    ((now - iter->second.m_accessTimestamp) >= m_nEvictionLastAccessThresholdSeconds))
                {
                    // Need to make sure TTL map and GroupID map are consistent
                    // Clear TTL map entry first
                    if (iter->second.m_expiryTime != 0)
                    {
                        auto ttlMapIter = m_vCacheTTLMap[bucket].find(iter->second.m_expiryTime);
                        if (ttlMapIter != m_vCacheTTLMap[bucket].end())
                        {
                            ttlMapIter->second.remove(iter->first);
                        }
                    }

                    // Remember this entry for deleting from the groupId map
                    if (!iter->second.m_groupId.empty())
                    {
                        vecGroupIdFilesToDelete[GetHashBucket(iter->second.m_groupId)]\
                        [iter->second.m_groupId].emplace_back(iter->first);
                    }

                    // Remove the cache entry
                    nNumFilesDeleted++;
                    nDeleteFileSize += iter->second.m_fileSize;
                    vecFilesToDelete.emplace_back(iter->first);

                    iter = m_vCacheInfoMap[bucket].erase(iter);
                }
                else
                {
                    iter++;
                }
            }

            // Before releasing the cacheInfoMap bucket lock, remove entries
            // from the groupId map buckets, these are two different sets of locks
            // so to minimize the number of times we acquire the GroupIdMapBucketLocks
            // we do it this way, and this also lets us avoid race conditions with
            // request processing threads
            for(int groupIdMapBucket = 0; groupIdMapBucket < NUM_MAPS; groupIdMapBucket++)
            {
                if (vecGroupIdFilesToDelete[groupIdMapBucket].empty())
                {
                    continue;
                }

                lock_guard groupIdBucketLock(m_vCacheGroupIDMapMutex[groupIdMapBucket]);
                for (auto& [groupId, filesToDelete] : vecGroupIdFilesToDelete[groupIdMapBucket])
                {
                    auto groupIdIter = m_vCacheGroupIDMap[groupIdMapBucket].find(groupId);
                    if (groupIdIter != m_vCacheGroupIDMap[groupIdMapBucket].end())
                    {
                        for (auto& fileName : filesToDelete)
                        {
                            groupIdIter->second.remove(fileName);
                        }
                    }
                }
            }
        }

        m_totalCachedSize.fetch_sub(nDeleteFileSize);
        LOG_INFO("Delete files. num files: {}, size: {}", nNumFilesDeleted, nDeleteFileSize);
        for (auto& filename : vecFilesToDelete)
        {
            // In the download path, we gracefully handle the case where cache entry
            // is present in the map, but absent on disk
            DeleteFile(filename);
        }
    }

    LOG_INFO("Shutdown EvictionWorker");
}
//---------------------------------------------------------------------------------------------------------------------
double CacheManager::GetCacheServerUtilizationPercentage() const
{
    return (double)m_totalCachedSize.load() * 100.0 / m_cacheSizeLimitBytes;
}
//---------------------------------------------------------------------------------------------------------------------