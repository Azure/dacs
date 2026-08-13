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
