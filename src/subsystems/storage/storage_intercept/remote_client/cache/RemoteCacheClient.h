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
#include "IAttributesCache.h"
#include "InMemoryAttributesCache.h"
#include "CacheClientImpl.h"
#include "ConnectionManager.h"
#include "CacheUploadManager.h"
#include "CacheUtils.h"
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
class RemoteCacheClient : public ICacheClient, public IAttributesCache
{
    public:
        /**
         * @param nMaxBufferSize The max size of the buffer that allowed for upload/download.
         *   When 0, the size is determined by Cache Client Impl.
         */
        RemoteCacheClient(/*[in] */ std::size_t nMaxBufferSize = MAX_BUFFER_SIZE_BYTES);
        ~RemoteCacheClient();

        cacheutils::CacheStatus DownloadSync(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& filePath,
            /*[in] */ const std::string& version,
            /*[in] */ off64_t fileOffset,
            /*[in] */ uint64_t startOffset,
            /*[in] */ uint64_t length,
            /*[out]*/ char* buffer,
            /*[out]*/ uint64_t& readLength) override;

        bool Upload(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& filePath,
            /*[in] */ const std::string& version,
            /*[in] */ off64_t fileOffset,
            /*[in] */ std::shared_ptr<const FileData> fileData,
            /*[in]*/ bool isWrite) override;

        bool DeleteGroup(const std::string& groupId) override;

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
        std::shared_ptr<CacheClientImpl>                m_spCacheClientImpl;
        std::unique_ptr<CacheUploadManager>             m_spUploadManager;
        uint32_t                                        m_numServerNodes;
        InMemoryAttributesCache                         m_inMemAttributeCache;
        const bool                                      m_bAttributesCachePutAllOnReaddir;
        const bool                                      m_bEnableVersionedCacheKeys;
        std::string                                     m_strChunkSizeSuffix;

        // Per mount specific parameters
        std::map<std::string, std::string>              m_mapEncryptionKey;
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
        std::string& GetEncryptionKeyForMount(/*[in] */ const std::string& mountName);

        std::string GetCacheKey(
            /*[in] */ const std::string& mountName,
            /*[in] */ const std::string& filePath,
            /*[in] */ const std::string& version,
            /*[in] */ off64_t fileOffset);

        std::string GetAttributeCacheKey(
            /*[in] */ const std::string& filePath);
};
//---------------------------------------------------------------------------------------------------------------------