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

#include "BlobClient.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptUtils.h"
#include "RemoteClientException.h"
#include "Retry.h"
#include <algorithm>
#include <dirent.h>
#include <functional>
#include "BlobAuthTokenProvider.h"
#include <dirent.h>
#include <random>
#include <sstream>
#include <mstream.h>
#include <atomic>
#include <utility>

// Global counter for tracking retries
static std::atomic<uint64_t> g_totalRetryCount{0};

//---------------------------------------------------------------------------------------------------------------------
// Refer: https://github.com/Azure/azure-storage-cpplite/blob/33a807a3984681a604c41659a4a2030ea81194e7/include/retry.h
//---------------------------------------------------------------------------------------------------------------------
class exponential_backoff_retry_policy final : public azure::storage_lite::retry_policy_base
{
  public:
    exponential_backoff_retry_policy()
        : m_maxRetryCount(StorageInterceptConfig::GetInstance().GetBlobRetryMaxAttempts())
        , m_retryIntervalBaseSeconds(StorageInterceptConfig::GetInstance().GetBlobRetryBaseDelaySeconds())
        , m_maxRetryIntervalSeconds(StorageInterceptConfig::GetInstance().GetBlobRetryMaxDelaySeconds())
    {
    }

    azure::storage_lite::retry_info evaluate(const azure::storage_lite::retry_context& context) const override
    {
        if (context.numbers() <= m_maxRetryCount && can_retry(context.result()))
        {
            auto interval = storageretry::GetRetryIntervalExpJitter(
                static_cast<uint32_t>(context.numbers()),
                std::chrono::seconds(m_retryIntervalBaseSeconds),
                std::chrono::seconds(m_maxRetryIntervalSeconds));
            
            LOG_INFO("Will retry attempts={} status-code={} in {} seconds (total_retry_done={})",
                     context.numbers(),
                     context.result(),
                     interval.count(),
                     ++g_totalRetryCount);

            return azure::storage_lite::retry_info(true, interval);
        }

        LOG_INFO("Will not retry attempts={} status-code={} (total_retry_done={})",
                 context.numbers(),
                 context.result(),
                 g_totalRetryCount.load());
        return azure::storage_lite::retry_info(false, std::chrono::seconds(0));
    }

  private:
    const uint32_t m_maxRetryCount;
    const uint32_t m_retryIntervalBaseSeconds;
    const uint32_t m_maxRetryIntervalSeconds;

    bool can_retry(azure::storage_lite::http_base::http_code statusCode) const
    {
        // 408 Request Timeout, 429 Too Many Requests, 500+ Server errors (except 501, 505)
        if (statusCode == 408 || statusCode == 429)
        {
            return true;
        }
        if (statusCode >= 500 && statusCode != 501 && statusCode != 505)
        {
            return true;
        }
        return false;
    }

};
//---------------------------------------------------------------------------------------------------------------------
BlobClient::BlobClient(const std::string& mountName)
    : m_strMountName(mountName)
    , m_bIsDynamicAccount(StorageInterceptConfig::GetInstance().IsDynamicAccountMount(mountName))
    , m_bIsDynamicContainer(StorageInterceptConfig::GetInstance().IsDynamicContainerMount(mountName))
    , m_isTokenFetchEnabled(false)
{
    GetConfigProperties();
    if (!m_bIsDynamicAccount)
    {
        auto client = std::make_shared<azBlobClient_t>(
            std::make_shared<azStorageAccount_t>(
                m_storageAccountContext.accountName,
                m_storageAuthContext.credential,
                USE_HTTPS,
                m_storageAccountContext.endpointUrl
            ),
            m_nMaxConnections
        );
        client->context()->set_retry_policy(std::make_shared<exponential_backoff_retry_policy>());
        m_mapBlobClients[""] = client;
    }

    LOG_INFO(
        "Initialized blob client with mountName={} accountName={}, maxConnections={},"
        " isDynamicAccount={} isDynamicContainer={} authType={} and isTokenFetchEnabled={} successfully",
        mountName,
        m_bIsDynamicAccount ? "" : m_storageAccountContext.accountName,
        m_nMaxConnections,
        m_bIsDynamicAccount,
        m_bIsDynamicContainer,
        std::to_underlying(m_storageAuthContext.authType),
        m_isTokenFetchEnabled);
}
//---------------------------------------------------------------------------------------------------------------------
const std::set<HttpStatus> BlobClient::m_setIgnorableErrors =
{
    /* Ignore read requests with offset = blob-size */
    HttpStatus::RangeNotSatisfiable
};
//---------------------------------------------------------------------------------------------------------------------
const std::string BlobClient::DIRECTORY_DELIMITER="/";
//---------------------------------------------------------------------------------------------------------------------
void BlobClient::GetConfigProperties()
{
    m_nMaxConnections =
        static_cast<size_t>(StorageInterceptConfig::GetInstance().GetMaxBlobConnectionsPerClient(m_strMountName));

    if (StorageInterceptConfig::GetInstance().GetMSIEndpoint(m_strMountName).length() != 0 &&
        StorageInterceptConfig::GetInstance().GetMSISecret(m_strMountName).length() != 0 &&
        StorageInterceptConfig::GetInstance().GetIdentityClientId(m_strMountName).length() != 0)
    {
        m_isTokenFetchEnabled = true;
    }

    // Also enable token fetch for workload identity (federated token file + tenant ID + client ID)
    if (StorageInterceptConfig::GetInstance().GetFederatedTokenFile(m_strMountName).length() != 0 &&
        StorageInterceptConfig::GetInstance().GetTenantId(m_strMountName).length() != 0 &&
        StorageInterceptConfig::GetInstance().GetIdentityClientId(m_strMountName).length() != 0)
    {
        m_isTokenFetchEnabled = true;
    }

    if (StorageInterceptConfig::GetInstance().GetUseAzureIdentitySDK(m_strMountName))
    {
        m_isTokenFetchEnabled = true;
    }

    if (m_bIsDynamicAccount)
    {
        if (StorageInterceptConfig::GetInstance().GetStorageSASToken(m_strMountName).length() != 0)
        {
            m_storageAuthContext = AzStorageAuthContext(
                AzStorageAuthType::SASToken,
                StorageInterceptConfig::GetInstance().GetStorageSASToken(m_strMountName));
        }
        else if (StorageInterceptConfig::GetInstance().GetBlobAccessToken(m_strMountName).length() != 0)
        {
            m_storageAuthContext = AzStorageAuthContext(
                AzStorageAuthType::AccessToken,
                StorageInterceptConfig::GetInstance().GetBlobAccessToken(m_strMountName));
        }
        else if (IsTokenRefreshed())
        {
            m_storageAuthContext = AzStorageAuthContext(
                AzStorageAuthType::AccessToken,
                m_strAccessToken);
        }
        else
        {
            throw RemoteClientException("ConfigurationError: Access token or SAS token is required for dynamic account mount");
        }

        return;
    }

    m_storageAccountContext = AzStorageAccountContext(
        StorageInterceptConfig::GetInstance().GetStorageAccountName(m_strMountName),
        StorageInterceptConfig::GetInstance().GetStorageEndpointUrl(m_strMountName)
    );
    
    if (StorageInterceptConfig::GetInstance().GetStorageAccountKey(m_strMountName).length() != 0)
    {
        m_storageAuthContext = AzStorageAuthContext(
            StorageInterceptConfig::GetInstance().GetStorageAccountName(m_strMountName),
            StorageInterceptConfig::GetInstance().GetStorageAccountKey(m_strMountName));
    }
    else if (StorageInterceptConfig::GetInstance().GetStorageSASToken(m_strMountName).length() != 0)
    {
        m_storageAuthContext = AzStorageAuthContext(
            AzStorageAuthType::SASToken,
            StorageInterceptConfig::GetInstance().GetStorageSASToken(m_strMountName));
    }
    else if (StorageInterceptConfig::GetInstance().GetBlobAccessToken(m_strMountName).length() != 0)
    {
        m_storageAuthContext = AzStorageAuthContext(
            AzStorageAuthType::AccessToken,
            StorageInterceptConfig::GetInstance().GetBlobAccessToken(m_strMountName));
        // TODO: add support for taking in managed identity params and fetch+renew access tokens.
    }
    else if (IsTokenRefreshed())
    {
        m_storageAuthContext = AzStorageAuthContext(
            AzStorageAuthType::AccessToken,
            m_strAccessToken);
    }
    else
    {
        throw RemoteClientException("ConfigurationError: auth credentials missing");
    }

    if (!m_bIsDynamicContainer)
    {
        m_strContainerName = StorageInterceptConfig::GetInstance().GetBlobContainerName(m_strMountName);
    }
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t BlobClient::ReadChunkSync(const std::string& remotePath, void* buf, size_t size, off_t offset)
{
    azure::storage_lite::omstream oStream((char *)buf, size);
    ssize_t nBytesRead = -1;
    auto blobOutcome = GetBlobClient(remotePath)->get_chunk_to_stream_sync(
        GetContainerName(remotePath),
        GetBlobName(remotePath),
        offset,
        size,
        oStream);

    if (!blobOutcome.success())
    {
        if (IsIgnorableError(
            static_cast<HttpStatus>(std::stoi(blobOutcome.error().code))))
        {
            LOG_DEBUG(
                "Ignoring the blob client error, path={}, requested offset={}, errorCode={}",
                remotePath,
                offset,
                blobOutcome.error().code);
            nBytesRead = 0;
            return nBytesRead;
        }

        LOG_ERROR(
            "Error occurred while reading blob bytes at path={}, container={}, blobPath={},"
            " status-code={}, code-name={}, error-msg={}",
            remotePath,
            GetContainerName(remotePath),
            GetBlobName(remotePath),
            blobOutcome.error().code,
            blobOutcome.error().code_name,
            blobOutcome.error().message);
        throw RemoteClientException("BlobClientError: " + blobOutcome.error().code_name + ", " + blobOutcome.error().message);
    }

    nBytesRead = static_cast<ssize_t>(blobOutcome.response().size);
    LOG_DEBUG("Read chunk API response received for path={}, nBytesRead={}", remotePath, nBytesRead);
    return nBytesRead;
}
//---------------------------------------------------------------------------------------------------------------------
std::string BlobClient::ReadDirectory(
  const std::string& remotePath,
  const std::string& continuationToken,
  std::vector<DirectoryEntry>& dirEntries)
{
    /* Case of ls on mount point for dynamic container mounts - we need to list containers insted of blobs Performing automatic code translation */
    if (m_bIsDynamicContainer && remotePath.empty())
    {
        auto listContainersOutcome = GetBlobClient(remotePath)->list_containers_segmented(
            "", /* prefix */
            continuationToken,
            DIRENTS_BATCH_SIZE).get();
        if (!listContainersOutcome.success())
        {
            LOG_ERROR(
                "Failed to list containers from remote for path={}"
                " status-code={}, code-name={}, error-msg={}",
                remotePath,
                listContainersOutcome.error().code,
                listContainersOutcome.error().code_name,
                listContainersOutcome.error().message);
            throw RemoteClientException("BlobClientError: " + listContainersOutcome.error().code_name + ", " + listContainersOutcome.error().message);
        }

        for (const auto& container : listContainersOutcome.response().containers)
        {
            dirEntries.push_back(DirectoryEntry());

            dirEntries.back().d_ino = D_INO_VAL;
            dirEntries.back().d_off = dirEntries.size() - 1;
            dirEntries.back().d_reclen = sizeof(DirectoryEntry);
            dirEntries.back().d_type = DT_DIR;
            strncpy(dirEntries.back().d_name, container.name.c_str(), D_NAME_LEN);
            dirEntries.back().d_size = DIRECTORY_SIZE;

            struct tm mTime;
            strptime(container.last_modified.c_str(), "%a, %d %b %Y %H:%M:%S", &mTime);
            // Response from blob only contains mTime
            dirEntries.back().d_atime = dirEntries.back().d_mtime = timegm(&mTime);
            dirEntries.back().d_version[0] = '\0';
        }

        return listContainersOutcome.response().next_marker;
    }

    const std::string blobPath = GetBlobName(remotePath);
    const std::string containerName = GetContainerName(remotePath);

    auto blobOutcome =
        GetBlobClient(remotePath)->list_blobs_segmented(
            containerName,
            DIRECTORY_DELIMITER,
            continuationToken,
            blobPath.size() == 0
                ? ""
                : blobPath.back() == DIRECTORY_DELIMITER.back()
                    ? blobPath
                    : blobPath + DIRECTORY_DELIMITER,
            DIRENTS_BATCH_SIZE).get();

    if (!blobOutcome.success())
    {
        LOG_ERROR(
            "Failed to list directory from remote for path={}, container={}, blobPath={},"
            " status-code={}, code-name={}, error-msg={}",
            remotePath,
            containerName,
            blobPath,
            blobOutcome.error().code,
            blobOutcome.error().code_name,
            blobOutcome.error().message);
        throw RemoteClientException("BlobClientError: " + blobOutcome.error().code_name + ", " + blobOutcome.error().message);
    }

    for (auto const& blob : blobOutcome.response().blobs)
    {
        std::string lastComponentName;
        if(blob.name.back() == DIRECTORY_DELIMITER.back())
        {
            lastComponentName =
                blob.name.substr(
                    blob.name.find_last_of(DIRECTORY_DELIMITER.back(),
                    blob.name.size() - 2) + 1);
            lastComponentName.pop_back();
        }
        else
        {
            lastComponentName = blob.name.substr(blob.name.find_last_of(DIRECTORY_DELIMITER.back()) + 1);
        }


        dirEntries.push_back(DirectoryEntry());
        dirEntries.back().d_ino = D_INO_VAL;
        dirEntries.back().d_off = dirEntries.size() - 1;
        dirEntries.back().d_reclen = sizeof(DirectoryEntry);
        dirEntries.back().d_type = blob.is_directory ? DT_DIR : DT_REG;

        strncpy(dirEntries.back().d_name, lastComponentName.c_str(), D_NAME_LEN);

        dirEntries.back().d_size = blob.content_length;
        struct tm mTime;
        strptime(blob.last_modified.c_str(), "%a, %d %b %Y %H:%M:%S", &mTime);
        // Response from blob only contains mTime
        dirEntries.back().d_atime = dirEntries.back().d_mtime = timegm(&mTime);

        // Extract dacsfileversion from blob metadata, fall back to etag
        std::string blobVersion;
        for (const std::pair<std::string, std::string>& kvp : blob.metadata)
        {
            if (kvp.first == StorageInterceptConstants::BlobMetadataKeyVersion)
            {
                blobVersion = kvp.second;
                break;
            }
        }
        if (blobVersion.empty() && !blob.is_directory)
        {
            blobVersion = blob.etag;
        }

        strncpy(dirEntries.back().d_version, blobVersion.c_str(), D_VERSION_LEN - 1);
        dirEntries.back().d_version[D_VERSION_LEN - 1] = '\0';
        if (!blobVersion.empty())
        {
            LOG_DEBUG("ReadDirectory: blob={} dacsfileversion={}", dirEntries.back().d_name, blobVersion);
        }
    }

    LOG_DEBUG(
        "Read directory API response recieved for path={}, numEntriesAdded={}, totalEntries={}",
        remotePath,
        blobOutcome.response().blobs.size(),
        dirEntries.size());
    return blobOutcome.response().next_marker;
}
//---------------------------------------------------------------------------------------------------------------------
int BlobClient::GetProperties(const std::string& remotePath, FileAttributes& attr)
{
    /* Case of stat on mount point for dynamic container mounts */
    if (m_bIsDynamicContainer && remotePath.empty())
    {
        MakeDirectoryAttr(attr);
        return 0;
    }

    auto blobClient = GetBlobClient(remotePath);
    const std::string blobPath = GetBlobName(remotePath);
    const std::string containerName = GetContainerName(remotePath);

    /* Case of root directory */
    if (blobPath.length() == 0)
    {
        MakeDirectoryAttr(attr);
        return 0;
    }

    std::vector<azBlobItem_t> listBlobResult = ListOneBlob(*blobClient, containerName, "", blobPath);

    if (blobPath.back() == DIRECTORY_DELIMITER.back())
    {
        if (listBlobResult.size() > 0)
        {
            // Directory exists as we got non-empty result for list blobs with queryPrefix with trailing delimiter.
            MakeDirectoryAttr(attr);
        }
        else
        {
            LOG_WARN("No directory found for container={} blobPath={}. Checking if a file exists with same name.",
                containerName,
                blobPath);
            const std::string blobPathWithoutTrailingDelimiter = blobPath.substr(0, blobPath.size() - 1);
            auto secondListBlobResult = ListOneBlob(*blobClient, containerName, "", blobPathWithoutTrailingDelimiter);
            if (secondListBlobResult.size() > 0 && secondListBlobResult[0].name == blobPathWithoutTrailingDelimiter)
            {
                LOG_WARN("File found instead of directory blobPath={}", blobPath);
                errno = ENOTDIR;
                return -1;
            }
            else
            {
                LOG_WARN("No file/dir found at blobPath={}", blobPathWithoutTrailingDelimiter);
                errno = ENOENT;
                return -1;
            }
        }
    }
    else
    {
        if (listBlobResult.size() == 0)
        {
            // No file\dir exists as we got empty response for list blobs with no trailing slash in query prefix.
            LOG_WARN("Blob not found in GetProperties. remotePath={}, blobPath={}",
                remotePath,
                blobPath);
            errno = ENOENT;
            return -1;
        }

        const auto& blobResponse = listBlobResult[0];
        if (blobResponse.name == blobPath)
        {
            /* blobPath corresponded to a file */
            MakeFileAttr(blobResponse, attr);
        }
        else if (blobResponse.name == (blobPath + DIRECTORY_DELIMITER))
        {
            /* blobPath corresponds to a valid directory */
            MakeDirectoryAttr(attr);
        }
        else
        {
            if (blobResponse.name.rfind(blobPath, 0) != 0)
            {
                LOG_ERROR(
                    "Possible memory corruption. List blob response items not matching query. response={}, query={}",
                    blobResponse.name,
                    blobPath);
                throw RemoteClientException("BlobClientError: Invalid blob path received in response");
            }

            const std::string blobPathWithTrailingDelimiter = blobPath + DIRECTORY_DELIMITER;
            bool directoryExists = false;
            if (blobResponse.name < blobPathWithTrailingDelimiter)
            { 
                // No file was found at blobPath but there may be a directory present over there alongwith some files
                // which have prefix blobPath but are lexicographically smaller than (blobPath + '/')
                LOG_WARN("No file found for container={} blobPath={}, Checking if a dir exists with same name.",
                    containerName,
                    blobPath);
                const std::string blobPathWithTrailingDelimiter = blobPath + DIRECTORY_DELIMITER;
                auto secondListBlobResult = ListOneBlob(*blobClient, containerName, "", blobPathWithTrailingDelimiter);
                if (secondListBlobResult.size() > 0)
                {
                    // Directory exists as we got non-empty result
                    directoryExists = true;
                }
            }

            if (directoryExists)
            {
                MakeDirectoryAttr(attr);
            }
            else
            {
                LOG_WARN("No file/dir found at blobPath={}, response path={}", blobPath, blobResponse.name);
                errno = ENOENT;
                return -1;
            }
        }
    }

    LOG_DEBUG("Get properties API response received for path={}, isDirectory={}, size={}",
      remotePath,
      attr.isDir,
      attr.size);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
void BlobClient::PrepareBlocksForUpload(
    const std::string& remotePath,
    size_t numBlocks)
{
    std::lock_guard<std::mutex> lock(m_lockBlockList);
    if(m_mapBlockList.find(remotePath) != m_mapBlockList.end())
    {
        LOG_ERROR("Block list already exists for path={}", remotePath);
        throw RemoteClientException("BlobClientError: Block list already exists for path=" + remotePath);
    }

    auto [blockListIter, inserted] = m_mapBlockList.emplace(remotePath, std::vector<azBlockItem_t>());
    blockListIter->second.reserve(numBlocks);

    std::string uuid = azure::storage_lite::get_uuid();
    for(size_t blockId = 0; blockId < numBlocks; blockId++)
    {
        std::string base64BlockId = GetUrlSafeBase64(uuid, blockId);
        blockListIter->second.emplace_back(azBlockItem_t{ std::move(base64BlockId), azBlockType_t::uncommitted });
    }

    return;
}
//---------------------------------------------------------------------------------------------------------------------
int BlobClient::StageBlocksForUpload(
    /*[in]*/ const std::string& remotePath,
    /*[in]*/ const void* buf,
    /*[in]*/ size_t length,
    /*[in]*/ off64_t blockId)
{
    std::shared_ptr<azBlobClient_t> blobClient = GetBlobClient(remotePath);
    const std::string blobPath = GetBlobName(remotePath);
    const std::string containerName = GetContainerName(remotePath);

    std::unique_lock<std::mutex> lock(m_lockBlockList);
    if(m_mapBlockList.find(remotePath) == m_mapBlockList.end())
    {
        LOG_ERROR("Block list not found for path={}", remotePath);
        throw RemoteClientException("BlobClientError: Block list not found for path=" + remotePath);
    }

    const std::string& blockIdBase64 = m_mapBlockList[remotePath][blockId].id;
    const int nTotalBlocks = static_cast<int>(m_mapBlockList[remotePath].size());
    lock.unlock();

    const auto blobOutcome = blobClient->upload_block_from_buffer(
        containerName,
        blobPath,
        blockIdBase64,
        (const char*)buf,
        length).get();

    if (!blobOutcome.success())
    {
        LOG_ERROR(
            "Error occurred while staging block for upload at path={}, container={}, blobPath={},"
            " block-id={} status-code={}, code-name={}, error-msg={}",
            remotePath,
            containerName,
            blobPath,
            blockId,
            blobOutcome.error().code,
            blobOutcome.error().code_name,
            blobOutcome.error().message);
        throw RemoteClientException("BlobClientError: " + blobOutcome.error().code_name + ", " + blobOutcome.error().message);
    }

    LOG_DEBUG("Staged block for upload at path={}, container={}, blobPath={}, block-id={}, nblock={}",
        remotePath,
        containerName,
        blobPath,
        blockId,
        nTotalBlocks);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int BlobClient::CommitBlocksForUpload(
    /*[in]*/ const std::string& remotePath,
    /*[in]*/ const std::string& version)
{
    std::shared_ptr<azBlobClient_t> blobClient = GetBlobClient(remotePath);
    const std::string blobPath = GetBlobName(remotePath);
    const std::string containerName = GetContainerName(remotePath);

    std::vector<std::pair<std::string, std::string>> metadata;
    if (!version.empty())
    {
        metadata.push_back({StorageInterceptConstants::BlobMetadataKeyVersion, version});
        LOG_DEBUG("CommitBlocksForUpload: setting metadata dacsfileversion={} for blob={}",
            version, remotePath);
    }
    std::vector<azBlockItem_t> blockList;
    
    {
        std::unique_lock<std::mutex> lock(m_lockBlockList);

        // If the block list is not found, we are uploading an empty blob
        auto it = m_mapBlockList.find(remotePath);
        if(it != m_mapBlockList.end())
        {
            blockList = std::move(it->second);
            m_mapBlockList.erase(it);
        }
    }

    const auto blobOutcome = blobClient->put_block_list(
        containerName,
        blobPath,
        blockList,
        metadata).get();

    if (!blobOutcome.success())
    {
        LOG_ERROR(
            "Error occurred while committing blocks for upload at path={}, container={}, blobPath={},"
            " status-code={}, code-name={}, error-msg={}",
            remotePath,
            containerName,
            blobPath,
            blobOutcome.error().code,
            blobOutcome.error().code_name,
            blobOutcome.error().message);
        throw RemoteClientException("BlobClientError: " + blobOutcome.error().code_name + ", " + blobOutcome.error().message);
    }

    LOG_INFO("Committed blocks for upload at path={}, container={}, blobPath={}, nBlocks={}",
        remotePath,
        containerName,
        blobPath,
        blockList.size());
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
std::vector<BlobClient::azBlobItem_t> BlobClient::ListOneBlob(
    azBlobClient_t& blobClient,
    const std::string& containerName,
    const std::string& continuationToken,
    const std::string& queryPrefix)
{
    const auto blobOutcome = blobClient.list_blobs_segmented(
        containerName,
        DIRECTORY_DELIMITER,
        continuationToken,
        queryPrefix,
        1 /* max results */).get();

    if (!blobOutcome.success())
    {
        LOG_ERROR(
            "Error during list one blob for container={} queryPrefix={} "
            "status-code={}, code-name={}, error-msg={}",
            containerName,
            queryPrefix,
            blobOutcome.error().code,
            blobOutcome.error().code_name,
            blobOutcome.error().message);
        
        if (blobOutcome.error().code_name == "ContainerNotFound")
        {
            LOG_WARN("Container not found in GetProperties. queryPrefix={}", queryPrefix);
            std::vector<azBlobItem_t> emptyBlobList;
            return emptyBlobList;
        }

        throw RemoteClientException("BlobClientError: " + blobOutcome.error().code_name + ", " + blobOutcome.error().message);
    }

    LOG_DEBUG("List one blob result for container={}, queryPrefix={}, continuationToken={}, nBlobs={}, blobName={}",
        containerName,
        queryPrefix,
        blobOutcome.response().next_marker,
        blobOutcome.response().blobs.size(),
        (blobOutcome.response().blobs.size() > 0) ? blobOutcome.response().blobs[0].name : "");
    
    // If blob result is empty but there are more results, we need to query again with new continuation token
    if (blobOutcome.response().blobs.empty() && blobOutcome.response().next_marker != "")
    {
        return ListOneBlob(blobClient, containerName, blobOutcome.response().next_marker, queryPrefix);
    }
    
    return blobOutcome.response().blobs;
}
//---------------------------------------------------------------------------------------------------------------------
void BlobClient::MakeFileAttr(const azBlobItem_t& blobItem, FileAttributes& attr)
{
    attr.isDir = false;
    attr.size = blobItem.content_length;
    struct tm mTime;
    strptime(blobItem.last_modified.c_str(), "%a, %d %b %Y %H:%M:%S", &mTime);
    attr.mTime.tv_sec = timegm(&mTime);

    // Response from blob only contains mTime, setting aTime/cTime to mTime
    attr.aTime = attr.cTime = attr.mTime;

    // Extract dacsfileversion from blob metadata, fall back to etag
    for (const std::pair<std::string, std::string>& kvp : blobItem.metadata)
    {
        if (kvp.first == StorageInterceptConstants::BlobMetadataKeyVersion)
        {
            attr.version = kvp.second;
            LOG_DEBUG("Found dacsfileversion metadata: {}", attr.version);
            break;
        }
    }
    if (attr.version.empty())
    {
        attr.version = blobItem.etag;
        LOG_DEBUG("No dacsfileversion, using etag as version: {}", attr.version);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void BlobClient::MakeDirectoryAttr(FileAttributes& attr)
{
    attr.isDir = true;
    attr.size = DIRECTORY_SIZE;
    timespec_get(&attr.mTime, TIME_UTC);

    /* Concept of directories is imaginary for blobs,
    * hence the directory blob size, modified times etc. are also imaginary.
    */
    attr.aTime = attr.cTime = attr.mTime;
}
//---------------------------------------------------------------------------------------------------------------------
bool BlobClient::IsIgnorableError(HttpStatus statusCode)
{
    bool isIgnorable = false;
    if (m_setIgnorableErrors.find(statusCode) != m_setIgnorableErrors.end())
    {
        isIgnorable = true;
    }
    return isIgnorable;
}
//---------------------------------------------------------------------------------------------------------------------
bool BlobClient::IsTokenRefreshed()
{
    if(!m_isTokenFetchEnabled)
    {
        return false;
    }

    std::string accessToken = BlobAuthTokenProvider::GetInstance().GetAccessToken(m_strMountName);
    if (!m_strAccessToken.empty() && (accessToken == m_strAccessToken))
    {
        LOG_DEBUG("Token is valid for mountName={}", m_strMountName);
        return false;
    }

    LOG_INFO("Updated token for mountName={}", m_strMountName);
    m_strAccessToken = accessToken;
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<azure::storage_lite::blob_client> BlobClient::GetBlobClient(/*[in] */ const std::string& remotePath)
{
    if (!m_bIsDynamicAccount)
    {
        std::lock_guard<std::mutex> lock(m_lockBlobClients);

        if(IsTokenRefreshed())
        {
            m_storageAuthContext= AzStorageAuthContext(
                AzStorageAuthType::AccessToken,
                m_strAccessToken);
            m_mapBlobClients[""] = std::make_shared<azBlobClient_t>(
                std::make_shared<azStorageAccount_t>(
                    m_storageAccountContext.accountName,
                    m_storageAuthContext.credential,
                    USE_HTTPS,
                    m_storageAccountContext.endpointUrl
                ),
                m_nMaxConnections
            );
            m_mapBlobClients[""]->context()->set_retry_policy(std::make_shared<exponential_backoff_retry_policy>());
            LOG_INFO("New blob client with updated token for mountName={}", m_strMountName);
        }
        
        // Static account, return the only blob client
        return m_mapBlobClients.begin()->second;
    }

    // Dynamic account, return the blob client corresponding to the accountHostName in remotePath
    const std::string accountHostname = StorageInterceptUtils::GetFirstComponentFromPath(remotePath);
    std::lock_guard<std::mutex> lock(m_lockBlobClients);

    auto itBlobClient = m_mapBlobClients.find(accountHostname);
    if (itBlobClient == m_mapBlobClients.end() || (IsTokenRefreshed()))
    {
        const std::string accountName = accountHostname.substr(0, accountHostname.find('.'));
        const std::string endpointUrl = "https://" + accountHostname;
        
        if(m_isTokenFetchEnabled)
        {
            m_storageAuthContext = AzStorageAuthContext(
                AzStorageAuthType::AccessToken,
                m_strAccessToken);
            
            m_mapBlobClients.clear();
        }

        LOG_INFO("Creating new blob client for account={}, endpoint={}", accountName, endpointUrl);
        m_mapBlobClients[accountHostname] = std::make_shared<azBlobClient_t>(
            std::make_shared<azStorageAccount_t>(
                accountName,
                m_storageAuthContext.credential,
                USE_HTTPS,
                endpointUrl
            ),
            m_nMaxConnections
        );
        
        itBlobClient = m_mapBlobClients.find(accountHostname);
        (*(itBlobClient->second)).context()->set_retry_policy(std::make_shared<exponential_backoff_retry_policy>());
    }

    return itBlobClient->second;
}
//---------------------------------------------------------------------------------------------------------------------
std::string BlobClient::GetContainerName(const std::string& remotePath) const
{
    if (m_bIsDynamicAccount)
    {
        // Remove the account hostname component and then get the container name from the remaining path.
        return StorageInterceptUtils::GetFirstComponentFromPath(StorageInterceptUtils::RemoveFirstComponentFromPath(remotePath));
    }

    return m_bIsDynamicContainer ? StorageInterceptUtils::GetFirstComponentFromPath(remotePath) : m_strContainerName;
}
//---------------------------------------------------------------------------------------------------------------------
std::string BlobClient::GetBlobName(const std::string& remotePath) const
{
    if (m_bIsDynamicAccount)
    {
        // Remove the account hostname component and then remove the container name component.
        return StorageInterceptUtils::RemoveFirstComponentFromPath(StorageInterceptUtils::RemoveFirstComponentFromPath(remotePath));
    }

    return m_bIsDynamicContainer ? StorageInterceptUtils::RemoveFirstComponentFromPath(remotePath) : remotePath;
}
//---------------------------------------------------------------------------------------------------------------------
std::string BlobClient::GetUrlSafeBase64(const std::string& uuid, const off64_t blockId) const
{
    // Logic derived from
    // https://github.com/Azure/azure-storage-cpplite/blob/33a807a3984681a604c41659a4a2030ea81194e7/src/blob/blob_client.cpp#L249
    // Note: Azure requires block ids to be base64 encoded strings of max length 64 bytes, we create block ids
    // by concatenating a uuid and block number with '-' padding in between to ensure fixed length before base64 encoding.
    std::string base64BlockId = std::to_string(blockId);
    base64BlockId = uuid + std::string(48 - uuid.length() - base64BlockId.length(), '-') + base64BlockId;
    base64BlockId = azure::storage_lite::to_base64(reinterpret_cast<const unsigned char*>(base64BlockId.data()), base64BlockId.length());

    return base64BlockId;
}
//---------------------------------------------------------------------------------------------------------------------