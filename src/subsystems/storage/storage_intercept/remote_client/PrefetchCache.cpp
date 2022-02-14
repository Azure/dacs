///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "PrefetchCache.h"
#include "StorageInterceptLogger.h"
//---------------------------------------------------------------------------------------------------------------------
PrefetchCache::PrefetchCache()
{
}
//---------------------------------------------------------------------------------------------------------------------
PrefetchCache& PrefetchCache::GetInstance()
{
    static PrefetchCache s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<const FileReadData> PrefetchCache::Get(const std::string& path, off64_t offset)
{
    std::unique_lock<std::mutex> buffersLock(m_mutexBuffers);

    auto it = m_mapBuffers.find(path);
    if (it == m_mapBuffers.end() || it->second->Offset != offset)
    {
        return nullptr;
    }

    return it->second;
}
//---------------------------------------------------------------------------------------------------------------------
void PrefetchCache::Put(const std::string& path, std::shared_ptr<const FileReadData> buffer)
{
    std::unique_lock<std::mutex> buffersLock(m_mutexBuffers);

    m_mapBuffers[path] = buffer;
    if (m_mapBuffers.size() > PrefetchCache::MAX_ENTRIES)
    {
        LOG_WARN("Too many entries in PrefetchCache. nEntries={}", m_mapBuffers.size());
    }
}
//---------------------------------------------------------------------------------------------------------------------
void PrefetchCache::Purge(const std::string& path)
{
    std::unique_lock<std::mutex> buffersLock(m_mutexBuffers);

    auto it = m_mapBuffers.find(path);
    if (it == m_mapBuffers.end())
    {
        return;
    }

    m_mapBuffers.erase(it);
    return;
}
//---------------------------------------------------------------------------------------------------------------------