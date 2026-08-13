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
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <shared_mutex>
#include "FileData.h"
#include <gtest/gtest.h>
//---------------------------------------------------------------------------------------------------------------------
/*
 * Cache to store the downloaded chunks of a file in memory.
 *
 * Note: To keep the total cache size in check, limits are enforced on number of entries per file and the total number
 *       of entries.
 */
class PrefetchCacheTest;
class PrefetchCache
{
    // Friends for testing
    FRIEND_TEST(PrefetchCacheTest, PrefetchCachePutGetPurgeTest);
    FRIEND_TEST(PrefetchCacheTest, PrefetchCacheVersionAwareTest);

    public:

        /* Singleton */
        static PrefetchCache& GetInstance();

        std::set<off64_t> GetCachedOffsets(
            /*[in]*/  const std::string& path,
            /*[in]*/  const std::string& version = "") const;

        std::shared_ptr<const FileData> Get(
            /*[in]*/  const std::string& path,
            /*[in]*/  off64_t offset,
            /*[in]*/  const std::string& version = "") const;

        void Put(
            /*[in]*/ const std::string& path,
            /*[in]*/ std::shared_ptr<const FileData> buffer,
            /*[in]*/ const std::string& version = "");

        void PurgeOffsets(
            /*[in] */ const std::string& path,
            /*[in] */ const std::vector<off64_t>& offsetsToPurge,
            /*[in] */ const std::string& version = "");

        void PurgeAll(
            /*[in] */ const std::string& path,
            /*[in] */ const std::string& version = "");

        // Limits on max entries per file and overall.
        const uint32_t    MAX_ENTRIES_PER_FILE;
        const uint32_t    MAX_ENTRIES_OVERALL;
    private:
        PrefetchCache();
        PrefetchCache(/*[in]*/ uint32_t maxEntriesPerFile, /*[in]*/ uint32_t maxEntriesOverall);

        static uint32_t CalculateNumEntries(/*[in]*/ uint64_t cacheSizeInMB);

        static std::string GetCacheKey(
            /*[in]*/  const std::string& path,
            /*[in]*/  off64_t offset,
            /*[in]*/  const std::string& version = "");

        static std::string GetPathKey(
            /*[in]*/  const std::string& path,
            /*[in]*/  const std::string& version = "");

        std::map<std::string, std::shared_ptr<const FileData>>        m_mapBuffers;
        std::map<std::string, std::set<off64_t>>                      m_mapCachedOffsets;
        mutable std::shared_mutex                                     m_mutexBuffers;
};
//---------------------------------------------------------------------------------------------------------------------
