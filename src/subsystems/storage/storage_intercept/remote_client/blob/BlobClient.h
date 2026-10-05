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
//---------------------------------------------------------------------------------------------------------------------
#include "AzStorageAccountContext.h"
#include "AzStorageAuthContext.h"
#include "IRemoteClient.h"
#include "HttpStatus.h"
#include <set>
#include <storage_account.h>
#include <map>
#include <mutex>
/* <blob/blob_client.h> uses syslog, that also defines the same macros defined in LoggingApi.h.
 * undef and redefine __linux__ to avoid conflict
 */
#ifdef __linux__
#undef __linux__
#include <blob/blob_client.h>
#include <base64.h>
#include <utility.h>
#define __linux__
#endif

#ifndef __linux__
#include <blob/blob_client.h>
#endif
//---------------------------------------------------------------------------------------------------------------------
class BlobClient : public IRemoteClient
{
    friend class BlobEndpointValidationTest;
    // TODO: move to the newer official sdk -> https://azure.github.io/azure-sdk-for-cpp/

    typedef azure::storage_lite::blob_client azBlobClient_t;
    typedef azure::storage_lite::storage_account azStorageAccount_t;
    typedef azure::storage_lite::list_blobs_segmented_item azBlobItem_t;
    typedef azure::storage_lite::put_block_list_request_base::block_type azBlockType_t;
    typedef azure::storage_lite::put_block_list_request_base::block_item azBlockItem_t;

    public:
        BlobClient(const std::string& mountName);

    public:
        ssize_t ReadChunkSync(
            /*[in] */ const std::string& remotePath,
            /*[out]*/ void *buf,
            /*[in] */ size_t length,
            /*[in] */ off_t offset) override;

        std::string ReadDirectory(
            /*[in] */ const std::string& remotePath,
            /*[in] */ const std::string& continuationToken,
            /*[out]*/ std::vector<DirectoryEntry>& dirEntries) override;

        int GetProperties(
            /*[in] */ const std::string& remotePath,
            /*[out]*/ FileAttributes& attr) override;

        void PrepareBlocksForUpload(
            /*[in]*/ const std::string& remotePath,
            /*[in]*/ size_t numBlocks) override;

        int StageBlocksForUpload(
            /*[in]*/ const std::string& remotePath,
            /*[in]*/ const void* buf,
            /*[in]*/ size_t length,
            /*[in]*/ off64_t blockId) override;
        
        int CommitBlocksForUpload(
            /*[in]*/ const std::string& remotePath,
            /*[in]*/ const std::string& version) override;

    private:
        void GetConfigProperties();

        bool IsIgnorableError(/*[in] */ HttpStatus statusCode);

        std::vector<azBlobItem_t> ListOneBlob(
            /*[in] */ azBlobClient_t& blobClient,
            /*[in] */ const std::string& containerName,
            /*[in] */ const std::string& continuationToken,
            /*[in] */ const std::string& queryPrefix);

        void MakeDirectoryAttr(/*[out]*/ FileAttributes& attr);

        void MakeFileAttr(
            /*[in]*/ const azBlobItem_t& blobItem,
            /*[out]*/ FileAttributes& attr);
        
        std::shared_ptr<azBlobClient_t> GetBlobClient(/*[in] */ const std::string& remotePath);

        std::string GetContainerName(/*[in] */ const std::string& remotePath) const;

        std::string GetBlobName(/*[in] */ const std::string& remotePath) const;

        std::string GetUrlSafeBase64(/*[in]*/ const std::string& uuid, /*[in] */ const off64_t blockId) const;

        bool IsTokenRefreshed();
    private:
        const std::string                                           m_strMountName;
        std::map<std::string, std::shared_ptr<azBlobClient_t>>      m_mapBlobClients;
        std::mutex                                                  m_lockBlobClients;
        AzStorageAccountContext                                     m_storageAccountContext;
        AzStorageAuthContext                                        m_storageAuthContext;
        const bool                                                  m_bIsDynamicAccount;
        const bool                                                  m_bIsDynamicContainer;
        std::string                                                 m_strContainerName;
        bool                                                        m_isTokenFetchEnabled;
        std::string                                                 m_strAccessToken;
        size_t                                                      m_nMaxConnections;
        static constexpr bool                                       USE_HTTPS=true;
        static constexpr uint64_t                                   DIRENTS_BATCH_SIZE=10000;
        static const std::string                                    DIRECTORY_DELIMITER;
        static const std::string                                    EMPTY_FILENAME;
        static constexpr size_t                                     DIRECTORY_SIZE=4096;

        // Key is remotepath, if multiple threads are trying to upload the same file using different file descriptors
        // then we need to use a different key
        std::map<std::string, std::vector<azBlockItem_t>>           m_mapBlockList;
        std::mutex                                                  m_lockBlockList;

        /* Static set containing <http-status>s' that could be ignored */
        static const std::set<HttpStatus>                           m_setIgnorableErrors;
};
//---------------------------------------------------------------------------------------------------------------------