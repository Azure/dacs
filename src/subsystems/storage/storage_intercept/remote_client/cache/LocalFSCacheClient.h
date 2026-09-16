// Copyright (c) Microsoft Corporation.
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
#include "ICacheClient.h"
#include <memory>
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
//---------------------------------------------------------------------------------------------------------------------
class LocalFSCacheClient : public ICacheClient
{
    public:
        LocalFSCacheClient();
        ~LocalFSCacheClient();

        cacheutils::CacheStatus DownloadSync(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& filePath,
            /*[in] */ const std::string& version,
            /*[in] */ off64_t fileOffset,
            /*[in] */ uint64_t startOffset,
            /*[in] */ uint64_t length,
            /*[out]*/ char *buffer,
            /*[out]*/ uint64_t& readLength) override;

        bool Upload(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& filePath,
            /*[in] */ const std::string& version,
            /*[in] */ off64_t fileOffset,
            /*[in] */ std::shared_ptr<const FileData> fileData,
            /*[in]*/ bool isWrite) override;

    private:

        // Local FS path for final cached data
        const std::string m_strLocalCachePath;

        // Staging path for data being uploaded, required to prevent partial data reads
        const std::string m_strLocalCacheTempPath;

        // Whether to use O_DIRECT flag for local writes, to bypass OS buffer cache
        const bool m_useODirectFlag;

        struct UploadContext
        {
            UploadContext(
                /*[in] */ const std::string& cacheKey,
                /*[in] */ std::shared_ptr<const FileData> fileData)
            : m_strCacheKey(cacheKey), m_fileData(fileData)
            {                
            }

            std::string                         m_strCacheKey;
            std::shared_ptr<const FileData> m_fileData;
        };

        // TODO: The async uploader pattern is repeated at multple places in code (remote and attr cache),
        //       we could refactor this for better code re-use
        
        // Manage async upload queue
        std::queue<std::unique_ptr<UploadContext>>     m_uploadQueue;
        std::mutex                                     m_mutexQueue;
        std::condition_variable                        m_cvQueue;

        // Manage upload workers
        const uint64_t                                 m_numUploadThreads;
        std::vector<std::unique_ptr<std::thread>>      m_vecUploadThread;
        std::atomic_uint64_t                           m_uploadBufferMemory;
        const uint64_t                                 m_uploadBufferMemoryWarnThreshold;
        uint64_t                                       m_maxUploadBufferMemory;
        std::atomic_bool                               m_stop;

        // Upload thread polling for requests to write to SSD
        void UploadWorker();

        // Write buffer to SSD synchronously
        bool UploadSync(
            /*[in] */ std::unique_ptr<UploadContext> uploadContext);

        // Check memory usage and log warnings
        void CheckBufferMemory();

        std::string GetCacheKey(/*[in] */ const std::string& filePath, /*[in] */ const std::string& version, /*[in] */ off64_t fileOffset);
};
//---------------------------------------------------------------------------------------------------------------------