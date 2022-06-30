///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "PrefetchCache.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptConfig.h"
//---------------------------------------------------------------------------------------------------------------------
PrefetchCache::PrefetchCache()
  : MAX_ENTRIES_PER_FILE(CalculateNumEntries(StorageInterceptConfig::GetInstance().GetPrefetchCachePerFileSizeInMB()))
  , MAX_ENTRIES_OVERALL(CalculateNumEntries(StorageInterceptConfig::GetInstance().GetPrefetchCacheTotalSizeInMB()))
{
    LOG_INFO("PrefetchCache initialized. MaxEntriesOverall={} MaxEntriesPerFile={}",
        MAX_ENTRIES_OVERALL,
        MAX_ENTRIES_PER_FILE);
}
//---------------------------------------------------------------------------------------------------------------------
PrefetchCache& PrefetchCache::GetInstance()
{
    static PrefetchCache s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
std::set<off64_t> PrefetchCache::GetCachedOffsets(const std::string& path) const
{
    std::shared_lock<std::shared_mutex> buffersLock(m_mutexBuffers);

    auto offsetIt = m_mapCachedOffsets.find(path);
    if (offsetIt == m_mapCachedOffsets.end()) return std::set<off64_t>{};

    return offsetIt->second;
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<const FileReadData> PrefetchCache::Get(const std::string& path, off64_t offset) const
{
    std::shared_lock<std::shared_mutex> buffersLock(m_mutexBuffers);

    auto offsetIt = m_mapCachedOffsets.find(path);
    if (offsetIt == m_mapCachedOffsets.end()) return nullptr;
    if (offsetIt->second.find(offset) == offsetIt->second.end()) return nullptr;

    std::string cacheKey = GetCacheKey(path, offset);
    auto it = m_mapBuffers.find(cacheKey);
    return it->second;
}
//---------------------------------------------------------------------------------------------------------------------
void PrefetchCache::Put(const std::string& path, std::shared_ptr<const FileReadData> buffer)
{
    std::unique_lock<std::shared_mutex> buffersLock(m_mutexBuffers);

    if (m_mapBuffers.size() >= MAX_ENTRIES_OVERALL)
    {
        LOG_WARN("Too many entries in PrefetchCache. nEntries={}", m_mapBuffers.size());
        return;
    }

    auto offsetIt = m_mapCachedOffsets.find(path);
    const size_t nCachedFileOffsets = (offsetIt == m_mapCachedOffsets.end()) ? 0 : offsetIt->second.size();
    if (nCachedFileOffsets >= MAX_ENTRIES_PER_FILE)
    {
        // Purge the first non-zero offset chunk to make space for the chunk being added right now.
        auto offsetToPurge = offsetIt->second.begin();
        if (*offsetToPurge == 0)   ++offsetToPurge;

        off64_t purgedOffset = *offsetToPurge;
        offsetIt->second.erase(offsetToPurge);
        LOG_DEBUG("Too many entries in PrefetchCache for file={}. nEntries={} putOffset={} purgedOffset={}",
            path,
            nCachedFileOffsets,
            buffer->Offset,
            purgedOffset);
    }

    m_mapBuffers[GetCacheKey(path, buffer->Offset)] = buffer;
    m_mapCachedOffsets[path].insert(buffer->Offset);
}
//---------------------------------------------------------------------------------------------------------------------
void PrefetchCache::PurgeOffsets(
    const std::string& path,
    const std::vector<off64_t>& offsetsToPurge)
{
    std::unique_lock<std::shared_mutex> buffersLock(m_mutexBuffers);

    auto offsetIt = m_mapCachedOffsets.find(path);
    if (offsetIt == m_mapCachedOffsets.end()) return;

    for (off64_t offset : offsetsToPurge)
    {
        m_mapBuffers.erase(GetCacheKey(path, offset));
        offsetIt->second.erase(offset);
    }

    if (offsetIt->second.size() == 0)
    {
        m_mapCachedOffsets.erase(offsetIt);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void PrefetchCache::PurgeAll(const std::string& path)
{
    std::unique_lock<std::shared_mutex> buffersLock(m_mutexBuffers);

    auto offsetIt = m_mapCachedOffsets.find(path);
    if (offsetIt == m_mapCachedOffsets.end()) return;

    for (off64_t offset : offsetIt->second)
    {
        m_mapBuffers.erase(GetCacheKey(path, offset));
    }

    m_mapCachedOffsets.erase(offsetIt);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t PrefetchCache::CalculateNumEntries(uint64_t cacheSizeInMB)
{
    return (uint32_t)((cacheSizeInMB * 1024 * 1024) / StorageInterceptConfig::GetInstance().GetStreamingChunkSize());
}
//---------------------------------------------------------------------------------------------------------------------
std::string PrefetchCache::GetCacheKey(const std::string& path, off64_t offset)
{
    return path + "##" + std::to_string(offset);
}
//---------------------------------------------------------------------------------------------------------------------