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
        static constexpr const char* LOCALHOST = "127.0.0.1";

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
