//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "ConnectionManager.h"
#include "EncryptionUtil.h"
#include "FileReadData.h"
#include <vector>
#include <queue>
#include <string>
#include <mutex>
#include <memory>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <condition_variable>
//---------------------------------------------------------------------------------------------------------------------
class CacheUploadManager
{
    public:
        CacheUploadManager(ConnectionManager& connectionManager, EncryptionUtil& encryptionUtil, int numUploadThreads,
            uint64_t maxTotalBufferSize);
        ~CacheUploadManager();

    public:

        /* Submit an upload job */
        bool Upload(int serverIndex, const std::string& cacheKey, std::shared_ptr<const FileReadData> fileData);

    private:
        struct UploadContext
        {
            UploadContext(int serverIndex, const std::string& cacheKey, std::shared_ptr<const FileReadData> fileData)
            : m_serverIndex(serverIndex), m_strCacheKey(cacheKey), m_fileData(fileData)
            {                
            }

            int                                 m_serverIndex;
            std::string                         m_strCacheKey;
            std::shared_ptr<const FileReadData> m_fileData;
        };

        std::atomic_uint64_t                           m_uploadBufferMemory;

        // TODO: implement per server limits
        std::queue<std::unique_ptr<UploadContext>>     m_uploadQueue;
        std::mutex                                     m_mutexQueue;
        std::condition_variable                        m_cvQueue;
        const uint64_t                                 m_maxBufferMemory;        

        const int                                      m_numUploadThreads;
        std::vector<std::unique_ptr<std::thread>>      m_vecUploadThread;
        std::atomic_bool                               m_stop;

        ConnectionManager&                             m_connectionManager;
        EncryptionUtil&                                m_encryptionUtil;

        void UploadWorker();
        bool StartUpload(std::unique_ptr<UploadContext> uploadContext);
};
//---------------------------------------------------------------------------------------------------------------------
