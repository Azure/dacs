//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "BlobClient.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptUtils.h"
#include "BlobClientException.h"
#include <dirent.h>
#include <sstream>
#include <mstream.h>
//---------------------------------------------------------------------------------------------------------------------
BlobClient::BlobClient()
    : m_bIsDynamic(StorageInterceptConfig::GetInstance().IsDynamicContainerMount())
{
    GetConfigProperties();
    m_spClient = std::make_unique<azBlobClient_t>(
        std::make_shared<azStorageAccount_t>(
            m_storageAccountContext.accountName,
            m_storageAuthContext.credential,
            USE_HTTPS,
            m_storageAccountContext.endpointUrl
        ),
        m_nMaxConnections
    );

    LOG_INFO(
        "Initialized blob client with accountName={}, maxConnections={},"
        " isDynamicMount={} and authType={} successfully",
        m_storageAccountContext.accountName,
        m_nMaxConnections,
        m_bIsDynamic,
        m_storageAuthContext.authType);
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
    m_storageAccountContext = AzStorageAccountContext(
        StorageInterceptConfig::GetInstance().GetStorageAccountName(),
        StorageInterceptConfig::GetInstance().GetStorageEndpointUrl()
    );
    
    if (StorageInterceptConfig::GetInstance().GetStorageAccountKey().length() != 0)
    {
        m_storageAuthContext = AzStorageAuthContext(
            StorageInterceptConfig::GetInstance().GetStorageAccountName(),
            StorageInterceptConfig::GetInstance().GetStorageAccountKey());
    }
    else if (StorageInterceptConfig::GetInstance().GetStorageSASToken().length() != 0)
    {
        m_storageAuthContext = AzStorageAuthContext(
            AzStorageAuthType::SASToken,
            StorageInterceptConfig::GetInstance().GetStorageSASToken());
    }
    else if (StorageInterceptConfig::GetInstance().GetBlobAccessToken().length() != 0)
    {
        m_storageAuthContext = AzStorageAuthContext(
            AzStorageAuthType::AccessToken,
            StorageInterceptConfig::GetInstance().GetBlobAccessToken());
        // TODO: add support for taking in managed identity params and fetch+renew access tokens.
    }
    else
    {
        throw BlobClientException("Blob auth credentials missing");
    }

    if (!m_bIsDynamic)
    {
        m_strContainerName = StorageInterceptConfig::GetInstance().GetBlobContainerName();
    }

    m_nMaxConnections = static_cast<size_t>(StorageInterceptConfig::GetInstance().GetMaxBlobConnectionsPerClient());
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t BlobClient::ReadChunkSync(const std::string& remotePath, void* buf, size_t size, off_t offset)
{
    azure::storage_lite::omstream oStream((char *)buf, size);
    ssize_t nBytesRead = -1;
    auto blobOutcome = m_spClient->get_chunk_to_stream_sync(
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
        throw BlobClientException(blobOutcome.error().message);
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
    const std::string blobPath = GetBlobName(remotePath);
    auto blobOutcome =
        m_spClient->list_blobs_segmented(
            GetContainerName(remotePath),
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
            GetContainerName(remotePath),
            blobPath,
            blobOutcome.error().code,
            blobOutcome.error().code_name,
            blobOutcome.error().message);
        throw BlobClientException(blobOutcome.error().message);
    }

    for (auto const& blob : blobOutcome.response().blobs)
    {
        dirEntries.push_back(DirectoryEntry());

        dirEntries.back().d_ino = D_INO_VAL;
        dirEntries.back().d_off = dirEntries.size() - 1;
        dirEntries.back().d_reclen = sizeof(DirectoryEntry);
        dirEntries.back().d_type = blob.is_directory ? DT_DIR : DT_REG;

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
        strncpy(dirEntries.back().d_name, lastComponentName.c_str(), D_NAME_LEN);

        dirEntries.back().d_size = blob.content_length;
        struct tm mTime;
        strptime(blob.last_modified.c_str(), "%a, %d %b %Y %H:%M:%S", &mTime);
        // Response from blob only contains mTime
        dirEntries.back().d_atime = dirEntries.back().d_mtime = timegm(&mTime);
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
    std::string emptyContinuationToken;
    /* We are only interested in getting one response */
    const int MAX_RESULTS = 1;

    const std::string blobPath = GetBlobName(remotePath);
    std::string listBlobsQueryPrefix = (blobPath.size() == 0) ? ""
        : blobPath.back() == DIRECTORY_DELIMITER.back()
            ? blobPath.substr(0, blobPath.size() - 1)
            : blobPath;

    const auto blobOutcome =
        m_spClient->list_blobs_segmented(
            GetContainerName(remotePath),
            DIRECTORY_DELIMITER,
            emptyContinuationToken,
            listBlobsQueryPrefix,
            MAX_RESULTS).get();

    if (!blobOutcome.success())
    {
        LOG_ERROR(
            "Unable to get blobs for path={}, container={}, blobPath={} status-code={}, code-name={}, error-msg={}",
            remotePath,
            GetContainerName(remotePath),
            blobPath,
            blobOutcome.error().code,
            blobOutcome.error().code_name,
            blobOutcome.error().message);
        
        if (blobOutcome.error().code_name == "ContainerNotFound")
        {
            LOG_WARN("Container not found in GetProperties. remotePath={}", remotePath);
            errno = ENOENT;
            return -1;
        }

        throw BlobClientException(blobOutcome.error().message);
    }

    if (blobOutcome.response().blobs.size() == 0)
    {
        LOG_WARN("Blob not found in GetProperties. remotePath={}, blobPath={}, queryPrefix={}",
            remotePath,
            blobPath,
            listBlobsQueryPrefix);
        errno = ENOENT;
        return -1;
    }

    /* Case of root directory */
    if (blobPath.length() == 0)
    {
        MakeDirectoryAttr(attr);
        return 0;
    }

    /* The response blobs list is ordered lexicographically (based on its name),
     * so an exact match with request path (if exists), would always occur first in the list
     */
    const auto& blobResponse = blobOutcome.response().blobs[0];
    if (blobPath.back() == DIRECTORY_DELIMITER.back())
    {
        if (blobResponse.name.compare(blobPath) == 0)
        {
            MakeDirectoryAttr(attr);
        }
        else
        {
            LOG_WARN("Invalid blob path provided {}, response path {}", blobPath, blobResponse.name);
            if (listBlobsQueryPrefix == blobResponse.name)
            {
                /*
                 * Case where remotePath is (valid-file-path + DIRECTORY_DELIMITER). It is still an invalid request,
                 * since a typical linux stat call also fails in such a scenario.
                 */
                errno = ENOTDIR;
            }
            else
            {
                /*
                 * Case where remotePath is a prefix to a valid path,
                 * but the sub-directory name after the last DIRECTORY_DELIMITER
                 * is not completely formed. eg: /mnt/DACS/dataplane/ a valid path
                 * and the requested path is /mnt/DACS/data/ ( ending '/' is removed before sending blob request).
                 * blobResponse for the above request is: /mnt/DACS/dataplane/
                 */
                errno = ENOENT;
            }

            return -1;
        }
    }
    else
    {
        if (blobResponse.name.compare(blobPath) == 0)
        {
            /* blobPath corresponded to a file */
            MakeFileAttr(blobResponse, attr);
        }
        else if (blobResponse.name.compare(blobPath + DIRECTORY_DELIMITER) == 0)
        {
            /* blobPath corresponds to a valid directory */
            MakeDirectoryAttr(attr);
        }
        else
        {
            if (blobResponse.name.rfind(blobPath, 0) != 0)
            {
                LOG_ERROR("Possible memory corruption. List blobs response={}, query={}",
                    blobResponse.name,
                    blobPath);
                throw BlobClientException("Invalid blob path received in response");
            }

            /* strict prefix of a valid path */
            LOG_WARN("Invalid blob path provided {}, response path {}", blobPath, blobResponse.name);
            errno = ENOENT;
            return -1;
        }
    }
    LOG_DEBUG("Get properties API response received for path={}, isDirectory={}, size={}", remotePath, attr.isDir, attr.size);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
void BlobClient::MakeFileAttr(const azBlobItem_t& blobItem, FileAttributes& attr)
{
    attr.isDir = false;
    attr.size = blobItem.content_length;
    struct tm mTime;
    strptime(blobItem.last_modified.c_str(), "%a, %d %b %Y %H:%M:%S", &mTime);
    attr.mTime.tv_sec = timegm(&mTime);

    /* Response from blob only contains mTime,
    * hence populating the unknown attributes in this manner
    */
    attr.aTime = attr.cTime = attr.mTime;
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
std::string BlobClient::GetContainerName(const std::string& remotePath) const
{
    return m_bIsDynamic ? StorageInterceptUtils::GetFirstComponentFromPath(remotePath) : m_strContainerName;
}
//---------------------------------------------------------------------------------------------------------------------
std::string BlobClient::GetBlobName(const std::string& remotePath) const
{
    return m_bIsDynamic ? StorageInterceptUtils::RemoveFirstComponentFromPath(remotePath) : remotePath;
}
//---------------------------------------------------------------------------------------------------------------------