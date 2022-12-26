///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "IAttributesCache.h"
#include "ConnectionManager.h"
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <condition_variable>
//---------------------------------------------------------------------------------------------------------------------
/*
 * Cache using local cacheserver APIs to store the file attributes fetched from remote store.
 */
class LocalAttributesCache : public IAttributesCache
{
    public:
        LocalAttributesCache();
        ~LocalAttributesCache();

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
        struct UploadContext
        {
            UploadContext(const std::string& cacheKey, const FileAttributes& fileAttributes)
            : m_strCacheKey(cacheKey), m_fileAttributes(fileAttributes)
            {                
            }

            const std::string                           m_strCacheKey;
            const FileAttributes                        m_fileAttributes;
        };

        static constexpr uint32_t                       COMBINE_QUEUE_ENTRIES = 100;
        static constexpr uint32_t                       QUEUE_MAX_WAIT_TIME_SECONDS = 2;

        std::unique_ptr<ConnectionManager>              m_spConnectionManager;
        std::queue<std::unique_ptr<UploadContext>>      m_uploadQueue;
        std::mutex                                      m_mutexQueue;
        std::condition_variable                         m_cvQueue;
        std::unique_ptr<std::thread>                    m_uploadThread;
        std::atomic_bool                                m_stop;

        void UploadWorker();
        bool StartUpload(std::queue<std::unique_ptr<UploadContext>>& uploadQueue);
};
//---------------------------------------------------------------------------------------------------------------------
