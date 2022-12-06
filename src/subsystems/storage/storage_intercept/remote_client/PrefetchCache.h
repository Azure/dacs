///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <shared_mutex>
#include "FileReadData.h"
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

    public:

        /* Singleton */
        static PrefetchCache& GetInstance();

        std::set<off64_t> GetCachedOffsets(/*[in]*/  const std::string& path) const;

        std::shared_ptr<const FileReadData> Get(
            /*[in]*/  const std::string& path,
            /*[in]*/  off64_t offset) const;

        void Put(
            /*[in]*/ const std::string& path,
            /*[in]*/ std::shared_ptr<const FileReadData> buffer);

        void PurgeOffsets(
            /*[in] */ const std::string& path,
            /*[in] */ const std::vector<off64_t>& offsetsToPurge);

        void PurgeAll(/*[in] */ const std::string& path);

        // Limits on max entries per file and overall.
        const uint32_t    MAX_ENTRIES_PER_FILE;
        const uint32_t    MAX_ENTRIES_OVERALL;
    private:
        PrefetchCache();
        PrefetchCache(/*[in]*/ uint32_t maxEntriesPerFile, /*[in]*/ uint32_t maxEntriesOverall);

        static uint32_t CalculateNumEntries(/*[in]*/ uint64_t cacheSizeInMB);

        static std::string GetCacheKey(
            /*[in]*/  const std::string& path,
            /*[in]*/  off64_t offset);

        std::map<std::string, std::shared_ptr<const FileReadData>>    m_mapBuffers;
        std::map<std::string, std::set<off64_t>>                      m_mapCachedOffsets;
        mutable std::shared_mutex                                     m_mutexBuffers;
};
//---------------------------------------------------------------------------------------------------------------------
