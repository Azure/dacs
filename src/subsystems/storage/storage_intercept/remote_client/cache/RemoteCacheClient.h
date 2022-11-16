//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "ICacheClient.h"
#include "ConnectionManager.h"
#include "CacheUploadManager.h"
#include "EncryptionUtil.h"
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
class RemoteCacheClient : public ICacheClient
{
    public:
        RemoteCacheClient();
        ~RemoteCacheClient();

        CacheStatus DownloadSync(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& cacheKey,
            /*[in] */ uint64_t startOffset,
            /*[in] */ uint64_t length,
            /*[out]*/ char* buffer,
            /*[out]*/ uint64_t& readLength) override;

        bool UploadAsync(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& cacheKey,
            /*[in] */ std::shared_ptr<const FileReadData> fileData) override;

    private:
        std::unique_ptr<ConnectionManager>              m_spConnectionManager;
        std::unique_ptr<CacheUploadManager>             m_spUploadManager;
        std::map<std::string, EncryptionUtil>           m_mapEncryptionUtil;
        int                                             m_numServerNodes;

        // Initialize the EncryptionUtil for each mount.
        // This function is not thread-safe and should only be called once from constructor
        void LoadEncryptionConfig();
        
        // Get the ref to the encryption util of a mount.
        // This function is thread-safe as long as LoadEncryptionConfig() is not being called cocurrently.
        EncryptionUtil& GetEncryptionUtilForMount(/*[in] */ const std::string& mountName);
};
//---------------------------------------------------------------------------------------------------------------------