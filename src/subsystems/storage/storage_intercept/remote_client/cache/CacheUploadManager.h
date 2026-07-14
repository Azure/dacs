//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "CacheClientImpl.h"
#include "ConnectionManager.h"
#include "FileData.h"
#include "FileAttributes.h"
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
        CacheUploadManager(
            std::shared_ptr<CacheClientImpl> spCacheClientImpl,
            int numUploadThreads,
            uint64_t maxTotalBufferSize);
        ~CacheUploadManager();

    public:

        /* Submit an upload job */
        bool Upload(
            const std::string& mountName,
            const std::string& cacheKey,
            std::shared_ptr<const FileData> fileData,
            const std::string& strEncryptionKey,
            std::unique_ptr<FileAttributes> fileAttributes,
            bool isWrite,
            const std::string& filePath = "",
            const std::string& version = "");

    private:
        struct UploadContext
        {
            UploadContext(
                const std::string& mountName,
                const std::string& cacheKey,
                std::shared_ptr<const FileData> fileData,
                const std::string& encryptionKey,
                std::unique_ptr<FileAttributes> fileAttributes,
                const std::string& filePath = "",
                const std::string& version = "")
            : m_strMountName(mountName)
            , m_strCacheKey(cacheKey)
            , m_fileData(fileData)
            , m_encryptionKey(encryptionKey)
            , m_fileAttributes(std::move(fileAttributes))
            , m_strFilePath(filePath)
            , m_strVersion(version)
            {
            }

            const std::string                   m_strMountName;
            const std::string                   m_strCacheKey;
            std::shared_ptr<const FileData>     m_fileData;
            const std::string                   m_encryptionKey;
            std::unique_ptr<FileAttributes>     m_fileAttributes;
            const std::string                   m_strFilePath;
            const std::string                   m_strVersion;
        };

        std::atomic_uint64_t                           m_uploadBufferMemory;

        // TODO: implement per server limits
        std::queue<std::unique_ptr<UploadContext>>     m_uploadQueue;
        std::mutex                                     m_mutexQueue;
        std::condition_variable                        m_cvQueue;
        const uint64_t                                 m_maxBufferMemory;
        const uint32_t                                 m_keyExpirySeconds;
        const std::string                              m_keyGroupId;
        const bool                                     m_enableSyncUpload;
        const bool                                     m_bEnableVersionedCacheKeys;

        const int                                      m_numUploadThreads;
        std::vector<std::unique_ptr<std::thread>>      m_vecUploadThread;
        std::atomic_bool                               m_stop;

        std::shared_ptr<CacheClientImpl>               m_spCacheClientImpl;

        void UploadWorker();
        bool StartUpload(std::unique_ptr<UploadContext> uploadContext);
        std::string GetEffectiveGroupId(const std::string& filePath, const std::string& version) const;
};
//---------------------------------------------------------------------------------------------------------------------
