///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IAttributesCache.h"
#include "FileAttributes.h"
#include "DirectoryEntry.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
/*
 * In memory cache to store the file attributes fetched from remote store.
 */
class InMemoryAttributesCache : public IAttributesCache
{
    public:
        InMemoryAttributesCache();
        InMemoryAttributesCache(uint64_t maxCacheEntries);        
        ~InMemoryAttributesCache();

        cacheutils::CacheStatus Get(
            /*[in] */ const std::string& path,
            /*[out]*/ FileAttributes& fileAttributes) override;

        void Put(
            /*[in] */ const std::string& path,
            /*[in] */ const FileAttributes& fileAttributes) override;

        void PutAll(
            /*[in] */ const std::string& pathPrefix,
            /*[in] */ const std::vector<DirectoryEntry>& dirEntries,
            /*[in] */ const size_t startIndex) override;

    private:
        // Map of relative path to file attributes
        std::unordered_map<std::string, FileAttributes>    m_mapAttributes;
        std::mutex                                         m_mutexAttributes;
        const uint64_t                                     m_maxCacheEntries;
};
//---------------------------------------------------------------------------------------------------------------------
