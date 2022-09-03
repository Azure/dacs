///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheManager.h"
#include "Constants.h"
#include "Utils.h"
#include "Logger.h"
#include <chrono>
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
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
  m_ttlWindowSizeSeconds(DefaultCacheTTLWindowSizeSeconds),
  m_cacheSizeLimitBytes(DefaultCacheSizeLimitBytes),
  m_totalCachedSize(0)
{
    m_cleanupThread = make_unique<thread>([this]() {
        this->CacheCleanupWorker();
    });
}
//---------------------------------------------------------------------------------------------------------------------
CacheManager::~CacheManager()
{
    m_stopCleanupThread = true;
    m_cvQueue.notify_all();
    m_cleanupThread->join();
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
    CacheInfo prevCacheStatus;
    auto index = GetHashBucket(fileName);

    lock_guard lockGuard(m_vCacheInfoMapMutex[index]);
    auto iter = m_vCacheInfoMap[index].find(fileName);
    if (iter == m_vCacheInfoMap[index].end())
    {
        m_vCacheInfoMap[index].insert(make_pair(fileName,
            CacheInfo(0, ::google::protobuf::Map<std::string, std::string>(), INITIATED)));
    }
    else
    {
        prevCacheStatus = iter->second;
    }
    return prevCacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
CacheManager::CacheInfo CacheManager::GetCacheInfo(const std::string& fileName)
{
    CacheInfo prevCacheStatus;
    auto index = GetHashBucket(fileName);

    lock_guard lockGuard(m_vCacheInfoMapMutex[index]);
    auto iter = m_vCacheInfoMap[index].find(fileName);
    if (iter != m_vCacheInfoMap[index].end())
    {
        prevCacheStatus = iter->second;
    }
    return prevCacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
bool CacheManager::InitCacheInfo(const std::string& fileName, bool ignoreLock, uint64_t fileSize)
{
    if (fileSize > 0 && (m_totalCachedSize.fetch_add(fileSize) + fileSize) > m_cacheSizeLimitBytes)
    {
        LOG_WARN("TotalCachedSize: {}, exceeds limit: {}", m_totalCachedSize, m_cacheSizeLimitBytes);
        m_totalCachedSize.fetch_sub(fileSize);
        return false;
    }

    auto index = GetHashBucket(fileName);
    unique_lock lockCache(m_vCacheInfoMapMutex[index]);
    auto iter = m_vCacheInfoMap[index].find(fileName);
    if (iter != m_vCacheInfoMap[index].end())
    {
        if (iter->second.m_status == COMPLETED)
        {
            // Some other upload for same file has already succeeded, ignore this one.
            LOG_WARN("File already uploaded by another thread: {}", fileName);
            m_totalCachedSize.fetch_sub(fileSize);
            return false;
        }
        iter->second = CacheInfo(STARTED);
    }
    else
    {
        if (ignoreLock)
        {
            m_vCacheInfoMap[index].insert(make_pair(fileName, CacheInfo(STARTED)));
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

    auto index = GetHashBucket(fileName);
    unique_lock lockCache(m_vCacheInfoMapMutex[index]);
    auto iter = m_vCacheInfoMap[index].find(fileName);
    if (iter != m_vCacheInfoMap[index].end())
    {
        if (iter->second.m_status == COMPLETED)
        {
            // Some other upload for same file has already succeeded, ignore this one.
            LOG_WARN("File already uploaded by another thread: {}", fileName);
            m_totalCachedSize.fetch_sub(fileSize);
            return false;
        }
        iter->second = CacheInfo(fileSize, metadata, COMPLETED);
    }
    else
    {
        m_vCacheInfoMap[index].insert(make_pair(fileName, CacheInfo(fileSize, metadata, COMPLETED)));
    }

    if (cacheExpirySeconds > 0)
    {
        time_t now = time(NULL);

        // Treat very large expiry values as no expiry, also avoids integer overflow
        if (cacheExpirySeconds >= (UINT32_MAX - now - m_ttlWindowSizeSeconds))
        {
            return true;
        }
        time_t timeBucket = now + cacheExpirySeconds;
        timeBucket = timeBucket - (timeBucket % m_ttlWindowSizeSeconds) + m_ttlWindowSizeSeconds;
        m_vCacheTTLMap[index][timeBucket].emplace_back(fileName);
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
void CacheManager::CacheCleanupWorker()
{
    LOG_INFO("Started CacheCleanupWorker. Sleep timer in seconds: {}", m_ttlWindowSizeSeconds);
    while (!m_stopCleanupThread)
    {
        // wait until timeout or shutdown notification
        std::unique_lock<std::mutex> lock(m_mutexQueue);
        if (m_cvQueue.wait_for(
            lock,
            m_ttlWindowSizeSeconds.load() * 1s,
            [this]
            {
                return (this->m_stopCleanupThread == true);
            }) == true)
        {
            break;
        }

        // copy the group ids to be deleted and release the queue lock
        list<string> groupDeleteList;
        while (!m_groupIdQueue.empty())
        {
            groupDeleteList.emplace_back(m_groupIdQueue.front());
            m_groupIdQueue.pop();
        }
        lock.unlock();

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
                    filesToDelete[bucket].swap(iter->second);
                    iter = m_vCacheTTLMap[bucket].erase(iter);
                }
                else
                {
                    break;
                }
            }
        }

        // Now include all keys from the groupid map
        for (string groupid: groupDeleteList)
        {
            int bucket = GetHashBucket(groupid);
            lock_guard lock(m_vCacheGroupIDMapMutex[bucket]);
            auto iter = m_vCacheGroupIDMap[bucket].find(groupid);
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

        // updating here before actual deletes to avoid failing new uploads
        m_totalCachedSize.fetch_sub(deleteFileSize);
        LOG_INFO("Delete files. num files: {}, size: {}", numFilesDeleted, deleteFileSize);

        // Now start deleting the files from disk
        for (int bucket = 0; bucket < NUM_MAPS; bucket++)
        {
            for (auto filename : filesToDelete[bucket])
            {
                DeleteFile(filename);
            }
        }
    }

    LOG_INFO("Shutdown CacheCleanupWorker");
}
//---------------------------------------------------------------------------------------------------------------------