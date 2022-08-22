//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "AzStorageAccountContext.h"
#include "AzStorageAuthContext.h"
#include "IRemoteClient.h"
#include "HttpStatus.h"
#include <set>
#include <storage_account.h>
/* <blob/blob_client.h> uses syslog, that also defines the same macros defined in LoggingApi.h.
 * undef and redefine __linux__ to avoid conflict
 */
#ifdef __linux__
#undef __linux__
#include <blob/blob_client.h>
#define __linux__
#endif

#ifndef __linux__
#include <blob/blob_client.h>
#endif
//---------------------------------------------------------------------------------------------------------------------
class BlobClient : public IRemoteClient
{
    // TODO: move to the newer official sdk -> https://azure.github.io/azure-sdk-for-cpp/

    typedef azure::storage_lite::blob_client azBlobClient_t;
    typedef azure::storage_lite::storage_account azStorageAccount_t;
    typedef azure::storage_lite::list_blobs_segmented_item azBlobItem_t;

    public:
        BlobClient();

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

    private:
        void GetConfigProperties();

        bool IsIgnorableError(
            /*[in] */ HttpStatus statusCode);

        void MakeDirectoryAttr(/*[out]*/ FileAttributes& attr);

        void MakeFileAttr(
            /*[in]*/ const azBlobItem_t& blobItem,
            /*[out]*/ FileAttributes& attr);

        std::string GetContainerName(/*[in] */ const std::string& remotePath) const;

        std::string GetBlobName(/*[in] */ const std::string& remotePath) const;

    private:
        std::unique_ptr<azBlobClient_t>                             m_spClient;
        AzStorageAccountContext                                     m_storageAccountContext;
        AzStorageAuthContext                                        m_storageAuthContext;
        const bool                                                  m_bIsDynamic;
        std::string                                                 m_strContainerName;
        size_t                                                      m_nMaxConnections;
        static constexpr bool                                       USE_HTTPS=true;
        static constexpr uint64_t                                   DIRENTS_BATCH_SIZE=10000;
        static const std::string                                    DIRECTORY_DELIMITER;
        static constexpr size_t                                     DIRECTORY_SIZE=4096;

        /* Static set containing <http-status>s' that could be ignored */
        static const std::set<HttpStatus>                           m_setIgnorableErrors;
};
//---------------------------------------------------------------------------------------------------------------------