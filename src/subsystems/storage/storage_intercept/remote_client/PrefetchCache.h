///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <memory>
#include <string>
#include <map>
#include <mutex>
#include "FileReadData.h"
//---------------------------------------------------------------------------------------------------------------------
/*
 * Cache to store the current chunk being read in memory.
 *
 * This type of caching would be helpful for workloads which do very small reads on a file sequentially. Any reads
 * which fall inside the cached chunk won't need to do a remote read.
 */
class PrefetchCache
{
    public:

        /* Singleton */
        static PrefetchCache& GetInstance();

        std::shared_ptr<const FileReadData> Get(
            /*[in]*/  const std::string& path,
            /*[in]*/  off64_t offset);

        void Put(
            /*[in]*/ const std::string& path,
            /*[in]*/ std::shared_ptr<const FileReadData> buffer);

        void Purge(/*[in] */ const std::string& path);

    private:
        PrefetchCache();

        std::map<std::string, std::shared_ptr<const FileReadData>>    m_mapBuffers;
        std::mutex                                                    m_mutexBuffers;
        static constexpr int                                          MAX_ENTRIES = 100;
};
//---------------------------------------------------------------------------------------------------------------------
