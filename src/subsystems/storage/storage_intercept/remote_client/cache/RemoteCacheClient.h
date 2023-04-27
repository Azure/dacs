//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "ICacheClient.h"
#include "IAttributesCache.h"
#include "InMemoryAttributesCache.h"
#include "ConnectionManager.h"
#include "CacheUploadManager.h"
#include "CacheUtils.h"
#include "EncryptionUtil.h"
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
class RemoteCacheClient : public ICacheClient, public IAttributesCache
{
    public:
        RemoteCacheClient();
        ~RemoteCacheClient();

        cacheutils::CacheStatus DownloadSync(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& filePath,
            /*[in] */ off64_t fileOffset,
            /*[in] */ uint64_t startOffset,
            /*[in] */ uint64_t length,
            /*[out]*/ char* buffer,
            /*[out]*/ uint64_t& readLength) override;

        bool Upload(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& filePath,
            /*[in] */ off64_t fileOffset,
            /*[in] */ std::shared_ptr<const FileReadData> fileData) override;

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
        std::unique_ptr<ConnectionManager>              m_spConnectionManager;
        std::unique_ptr<CacheUploadManager>             m_spUploadManager;
        int                                             m_numServerNodes;
        InMemoryAttributesCache                         m_inMemAttributeCache;

        // Per mount specific parameters
        std::map<std::string, EncryptionUtil>           m_mapEncryptionUtil;
        std::map<std::string, std::string>              m_strCachePrefixMap;

        cacheutils::CacheStatus DownloadWithMetadataSync(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& cacheKey,
            /*[in] */ uint64_t startOffset,
            /*[in] */ uint64_t length,
            /*[out]*/ char* buffer,
            /*[out]*/ uint64_t& readLength,
            /*[out]*/ FileAttributes* fileAttributes);

        // Initialize the EncryptionUtil and cahceKeyPrefix for each mount.
        // This function is not thread-safe and should only be called once from constructor
        void LoadMultiMountConfig();
        
        // Get the ref to the encryption util of a mount.
        // This function is thread-safe as long as LoadEncryptionConfig() is not being called cocurrently.
        EncryptionUtil& GetEncryptionUtilForMount(/*[in] */ const std::string& mountName);

        std::string GetCacheKey(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& filePath,
            /*[in] */ off64_t fileOffset);

        std::string GetAttributeCacheKey(
            /*[in] */ const std::string& filePath);
};
//---------------------------------------------------------------------------------------------------------------------