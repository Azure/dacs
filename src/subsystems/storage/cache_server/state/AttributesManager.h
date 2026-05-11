///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "cache.pb.h"
#include "Utils.h"
#include "Constants.h"
#include "Logger.h"
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>
#include <string>
#include <sys/types.h>
//---------------------------------------------------------------------------------------------------------------------
class AttributesManager
{
    public:
        static AttributesManager& GetInstance();

        struct CacheAttributes
        {
            CacheAttributes(bool isdir, int64_t filesize, uint64_t accessedtime, uint64_t modifiedtime,
                            const std::string& version = "")
                : m_isdir(isdir), m_filesize(filesize), m_accessedtime(accessedtime), m_modifiedtime(modifiedtime),
                  m_version(version) {}
            CacheAttributes() 
                : m_isdir(false), m_filesize(-1), m_accessedtime(0), m_modifiedtime(0) {}

            bool m_isdir;
            int64_t m_filesize;
            uint64_t m_accessedtime;
            uint64_t m_modifiedtime;
            std::string m_version;
        };

        void SetAttributeCacheLimit(
            /*[in] */ const uint64_t limitCacheEntries);

        // Fetch cached attributes and initialize (to coordinate multiple updates from clients) if it does not exist
        bool GetAndInitAttribute(
            /*[in] */ const std::string& fileName,
            /*[out]*/ CacheAttributes& fileAttributes);

        // Fetch cached attributes
        bool GetAttribute(
            /*[in] */ const std::string& fileName,
            /*[out]*/ CacheAttributes& fileAttributes);

        // Update cached attributes
        bool PutAttributes(
            /*[in] */ const cache::PutAttributeRequest& putAttributeRequest);

    private:
        AttributesManager();
        ~AttributesManager();

        // Bucketized attributes cache map
        std::vector<std::unordered_map<std::string, CacheAttributes>> m_vAttributesMap;

        // mutexes for the cache buckets
        std::vector<std::mutex> m_vAttributesMutex;

        std::atomic_uint_fast64_t m_anAttributeCacheLimit;
        std::atomic_uint_fast64_t m_anTotalCachedAttributes;
};
//---------------------------------------------------------------------------------------------------------------------