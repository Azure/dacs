///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "InMemoryAttributesCache.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptUtils.h"
#include <dirent.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace cacheutils;
//---------------------------------------------------------------------------------------------------------------------
InMemoryAttributesCache::InMemoryAttributesCache()
: m_maxCacheEntries(StorageInterceptConfig::GetInstance().GetAttributesCacheMaxEntries())
{
    LOG_INFO("Using InMemoryAttributesCache for caching remote file properties. Max cache entries: {}",
		    m_maxCacheEntries);
}
//---------------------------------------------------------------------------------------------------------------------
InMemoryAttributesCache::InMemoryAttributesCache(uint64_t maxCacheEntries)
: m_maxCacheEntries(maxCacheEntries)
{
    LOG_INFO("Using InMemoryAttributesCache for caching remote file properties. Max cache entries: {}",
       m_maxCacheEntries);
}
//---------------------------------------------------------------------------------------------------------------------
InMemoryAttributesCache::~InMemoryAttributesCache()
{
}
//---------------------------------------------------------------------------------------------------------------------
CacheStatus InMemoryAttributesCache::Get(const std::string& path, FileAttributes& fileAttributes)
{
    std::lock_guard<std::mutex> lock(m_mutexAttributes);
    auto cacheEntry = m_mapAttributes.find(path);
    if (cacheEntry == m_mapAttributes.end())
    {
        return CacheStatus::NOTFOUND_LOCKED;
    }
    fileAttributes = cacheEntry->second;
    return CacheStatus::SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
void InMemoryAttributesCache::Put(const std::string& path, const FileAttributes& fileAttributes)
{
    // Update the entry to latest value
    std::lock_guard<std::mutex> lock(m_mutexAttributes);
    if (m_mapAttributes.size() >= m_maxCacheEntries)
    {
        LOG_WARN("Reached max atrributes cache value, ignoring extra entries");
        return;
    }
    m_mapAttributes[path] = fileAttributes;
}
//---------------------------------------------------------------------------------------------------------------------
void InMemoryAttributesCache::PutAll(
    const std::string& pathPrefix,
    const std::vector<DirectoryEntry>& dirEntries,
    const size_t startIndex)
{
    int numEntries = dirEntries.size() - startIndex;
    if (numEntries <= 0)
    {
        return;
    }

    // Update the new entries to latest value
    std::lock_guard<std::mutex> lock(m_mutexAttributes);
    if ((numEntries + m_mapAttributes.size()) > m_maxCacheEntries)
    {
        LOG_RARELY(30, WARN, "Reached max attributes cache value, ignoring extra entries");
        numEntries = m_maxCacheEntries - m_mapAttributes.size();
    }

    LOG_DEBUG("Adding {} entries to attribute cache", numEntries);
    for (size_t index = startIndex; index < startIndex + numEntries; index++)
    {
        const DirectoryEntry& dirEntry = dirEntries[index];
        FileAttributes fileAttributes;
        fileAttributes.isDir = (dirEntry.d_type == DT_DIR);
        fileAttributes.size = dirEntry.d_size;
        fileAttributes.mTime.tv_sec = dirEntry.d_mtime;
        fileAttributes.aTime.tv_sec = dirEntry.d_atime;
        fileAttributes.cTime = fileAttributes.mTime;
        fileAttributes.version = std::string(dirEntry.d_version);

        const std::string strPath = StorageInterceptUtils::CombinePath(pathPrefix, dirEntry.d_name);
        m_mapAttributes[strPath] = fileAttributes;
    }
}
//---------------------------------------------------------------------------------------------------------------------
