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

#include "SdkBlobClient.h"
#include "DacsTokenCredential.h"

#include "StorageInterceptConfig.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptUtils.h"
#include "RemoteClientException.h"
#include "DirectoryEntry.h"
#include "FileAttributes.h"
#include "Retry.h"

#include <azure/storage/blobs.hpp>
#include <azure/core/io/body_stream.hpp>
#include <azure/core/uuid.hpp>
#include <azure/core/base64.hpp>

#include <chrono>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
//---------------------------------------------------------------------------------------------------------------------
namespace Blobs = Azure::Storage::Blobs;
//---------------------------------------------------------------------------------------------------------------------
namespace
{
    constexpr uint64_t DIRENTS_BATCH_SIZE = 10000;
    constexpr size_t DIRECTORY_SIZE = 4096;
    const std::string DIRECTORY_DELIMITER = "/";

    // Wraps a fixed, non-refreshable access token for the SDK auth pipeline.
    class StaticBearerTokenCredential final : public Azure::Core::Credentials::TokenCredential
    {
        public:
            explicit StaticBearerTokenCredential(std::string token)
                : Azure::Core::Credentials::TokenCredential("StaticBearerTokenCredential")
                , m_token(std::move(token))
            {
            }

            Azure::Core::Credentials::AccessToken GetToken(
                const Azure::Core::Credentials::TokenRequestContext&,
                const Azure::Core::Context&) const override
            {
                Azure::Core::Credentials::AccessToken accessToken;
                accessToken.Token = m_token;
                // Nothing to refresh to; report a far-future expiry to avoid needless GetToken calls.
                accessToken.ExpiresOn = Azure::DateTime(
                    std::chrono::system_clock::now() + std::chrono::hours(24 * 365));
                return accessToken;
            }

        private:
            const std::string m_token;
    };

    std::string ExtractLastComponent(const std::string& name)
    {
        std::string trimmed = name;
        if (!trimmed.empty() && trimmed.back() == DIRECTORY_DELIMITER.back())
        {
            trimmed.pop_back();
        }
        const size_t pos = trimmed.find_last_of(DIRECTORY_DELIMITER.back());
        return (pos == std::string::npos) ? trimmed : trimmed.substr(pos + 1);
    }

    time_t ToTimeT(const Azure::DateTime& dateTime)
    {
        return std::chrono::system_clock::to_time_t(
            static_cast<std::chrono::system_clock::time_point>(dateTime));
    }

    // Returns the dacsfileversion from blob metadata, falling back to the ETag when absent.
    std::string ExtractBlobVersion(const Blobs::Models::BlobItem& blob)
    {
        for (const auto& kvp : blob.Details.Metadata)
        {
            if (kvp.first == StorageInterceptConstants::BlobMetadataKeyVersion)
            {
                return kvp.second;
            }
        }
        return blob.Details.ETag.ToString();
    }
}
//---------------------------------------------------------------------------------------------------------------------
SdkBlobClient::SdkBlobClient(const std::string& mountName)
    : m_strMountName(mountName)
    , m_bIsDynamicAccount(StorageInterceptConfig::GetInstance().IsDynamicAccountMount(mountName))
    , m_bIsDynamicContainer(StorageInterceptConfig::GetInstance().IsDynamicContainerMount(mountName))
    , m_bChecksumEnabled(StorageInterceptConfig::GetInstance().GetRemoteBlobChecksumEnabled(mountName))
    , m_bUploadChecksumEnabled(StorageInterceptConfig::GetInstance().GetRemoteBlobUploadChecksumEnabled(mountName))
    , m_nChecksumRetryMaxAttempts(StorageInterceptConfig::GetInstance().GetRemoteBlobChecksumRetryMaxAttempts())
    , m_blobRetryBaseDelay(std::chrono::seconds(StorageInterceptConfig::GetInstance().GetBlobRetryBaseDelaySeconds()))
    , m_blobRetryMaxDelay(std::chrono::seconds(StorageInterceptConfig::GetInstance().GetBlobRetryMaxDelaySeconds()))
{
    m_isTokenFetchEnabled = DetectTokenFetchEnabled();
    ResolveAuth();

    if (!m_bIsDynamicAccount)
    {
        std::lock_guard<std::mutex> lock(m_lockServiceClients);
        m_mapServiceClients[""] = BuildServiceClient(m_endpointUrl);
    }

    LOG_INFO(
        "Initialized SdkBlobClient mountName={} isDynamicAccount={} isDynamicContainer={} "
        "downloadChecksumEnabled={} uploadChecksumEnabled={} tokenFetchEnabled={}",
        mountName, m_bIsDynamicAccount, m_bIsDynamicContainer, m_bChecksumEnabled,
        m_bUploadChecksumEnabled, m_isTokenFetchEnabled);
}
//---------------------------------------------------------------------------------------------------------------------
bool SdkBlobClient::DetectTokenFetchEnabled() const
{
    StorageInterceptConfig& cfg = StorageInterceptConfig::GetInstance();

    if (cfg.GetMSIEndpoint(m_strMountName).length() != 0
        && cfg.GetMSISecret(m_strMountName).length() != 0
        && cfg.GetIdentityClientId(m_strMountName).length() != 0)
    {
        return true;
    }
    if (cfg.GetFederatedTokenFile(m_strMountName).length() != 0
        && cfg.GetTenantId(m_strMountName).length() != 0
        && cfg.GetIdentityClientId(m_strMountName).length() != 0)
    {
        return true;
    }
    if (cfg.GetUseAzureIdentitySDK(m_strMountName))
    {
        return true;
    }
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
void SdkBlobClient::ResolveAuth()
{
    StorageInterceptConfig& cfg = StorageInterceptConfig::GetInstance();

    if (m_bIsDynamicAccount)
    {
        // Dynamic account: endpoint is derived per request from the account hostname in the path.
        if (cfg.GetStorageSASToken(m_strMountName).length() != 0)
        {
            m_authKind = AuthKind::Sas;
            m_sasToken = cfg.GetStorageSASToken(m_strMountName);
        }
        else if (cfg.GetBlobAccessToken(m_strMountName).length() != 0)
        {
            m_authKind = AuthKind::Token;
            m_staticAccessToken = cfg.GetBlobAccessToken(m_strMountName);
            m_tokenCredential = std::make_shared<StaticBearerTokenCredential>(m_staticAccessToken);
        }
        else if (m_isTokenFetchEnabled)
        {
            m_authKind = AuthKind::Token;
            m_tokenCredential = std::make_shared<DacsTokenCredential>(m_strMountName);
        }
        else
        {
            throw RemoteClientException(
                "ConfigurationError: Access token or SAS token is required for dynamic account mount");
        }
        return;
    }

    // Static account
    m_accountName = cfg.GetStorageAccountName(m_strMountName);
    m_endpointUrl = cfg.GetStorageEndpointUrl(m_strMountName);

    if (cfg.GetStorageAccountKey(m_strMountName).length() != 0)
    {
        m_authKind = AuthKind::SharedKey;
        m_accountKey = cfg.GetStorageAccountKey(m_strMountName);
    }
    else if (cfg.GetStorageSASToken(m_strMountName).length() != 0)
    {
        m_authKind = AuthKind::Sas;
        m_sasToken = cfg.GetStorageSASToken(m_strMountName);
    }
    else if (cfg.GetBlobAccessToken(m_strMountName).length() != 0)
    {
        m_authKind = AuthKind::Token;
        m_staticAccessToken = cfg.GetBlobAccessToken(m_strMountName);
        m_tokenCredential = std::make_shared<StaticBearerTokenCredential>(m_staticAccessToken);
    }
    else if (m_isTokenFetchEnabled)
    {
        m_authKind = AuthKind::Token;
        m_tokenCredential = std::make_shared<DacsTokenCredential>(m_strMountName);
    }
    else
    {
        throw RemoteClientException("ConfigurationError: auth credentials missing");
    }

    if (!m_bIsDynamicContainer)
    {
        m_strContainerName = cfg.GetBlobContainerName(m_strMountName);
    }
}
//---------------------------------------------------------------------------------------------------------------------
Blobs::BlobClientOptions SdkBlobClient::MakeClientOptions() const
{
    StorageInterceptConfig& cfg = StorageInterceptConfig::GetInstance();
    Blobs::BlobClientOptions options;
    const uint32_t configuredAttempts = cfg.GetBlobRetryMaxAttempts();
    options.Retry.MaxRetries = configuredAttempts > 0 ? static_cast<int32_t>(configuredAttempts - 1) : 0;
    options.Retry.RetryDelay = std::chrono::seconds(cfg.GetBlobRetryBaseDelaySeconds());
    options.Retry.MaxRetryDelay = std::chrono::seconds(cfg.GetBlobRetryMaxDelaySeconds());

    // The SDK defaults already retry the standard transient codes (408/500/502/503/504). cpplite
    // also retried every other 5xx (except 501/505); of those we add only 506/507. The rest
    // (508 LoopDetected / 510 NotExtended / 511 NetworkAuthenticationRequired) are WebDAV / captive-
    // portal codes Azure Storage never returns and that a retry cannot resolve, so we omit them.
    using Azure::Core::Http::HttpStatusCode;
    options.Retry.StatusCodes.insert(HttpStatusCode::VariantAlsoNegotiates); // 506
    options.Retry.StatusCodes.insert(HttpStatusCode::InsufficientStorage);   // 507
    return options;
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<Blobs::BlobServiceClient> SdkBlobClient::BuildServiceClient(const std::string& endpointUrl)
{
    Blobs::BlobClientOptions options = MakeClientOptions();

    switch (m_authKind)
    {
        case AuthKind::SharedKey:
        {
            auto cred = std::make_shared<Azure::Storage::StorageSharedKeyCredential>(m_accountName, m_accountKey);
            return std::make_shared<Blobs::BlobServiceClient>(endpointUrl, cred, options);
        }
        case AuthKind::Token:
        {
            return std::make_shared<Blobs::BlobServiceClient>(endpointUrl, m_tokenCredential, options);
        }
        case AuthKind::Sas:
        default:
        {
            const std::string sep = (!m_sasToken.empty() && m_sasToken.front() == '?') ? "" : "?";
            return std::make_shared<Blobs::BlobServiceClient>(endpointUrl + sep + m_sasToken, options);
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<Blobs::BlobServiceClient> SdkBlobClient::GetServiceClient(const std::string& remotePath)
{
    if (!m_bIsDynamicAccount)
    {
        std::lock_guard<std::mutex> lock(m_lockServiceClients);
        return m_mapServiceClients.begin()->second;
    }

    const std::string accountHostname = StorageInterceptUtils::GetFirstComponentFromPath(remotePath);

    std::lock_guard<std::mutex> lock(m_lockServiceClients);
    auto it = m_mapServiceClients.find(accountHostname);
    if (it == m_mapServiceClients.end())
    {
        const std::string endpointUrl = "https://" + accountHostname;
        LOG_INFO("Creating new SDK blob service client for endpoint={}", endpointUrl);
        it = m_mapServiceClients.emplace(accountHostname, BuildServiceClient(endpointUrl)).first;
    }
    return it->second;
}
//---------------------------------------------------------------------------------------------------------------------
Blobs::BlobContainerClient SdkBlobClient::GetContainerClient(const std::string& remotePath)
{
    return GetServiceClient(remotePath)->GetBlobContainerClient(GetContainerName(remotePath));
}
//---------------------------------------------------------------------------------------------------------------------
std::string SdkBlobClient::GetContainerName(const std::string& remotePath) const
{
    if (m_bIsDynamicAccount)
    {
        return StorageInterceptUtils::GetFirstComponentFromPath(
            StorageInterceptUtils::RemoveFirstComponentFromPath(remotePath));
    }
    return m_bIsDynamicContainer
        ? StorageInterceptUtils::GetFirstComponentFromPath(remotePath)
        : m_strContainerName;
}
//---------------------------------------------------------------------------------------------------------------------
std::string SdkBlobClient::GetBlobName(const std::string& remotePath) const
{
    if (m_bIsDynamicAccount)
    {
        return StorageInterceptUtils::RemoveFirstComponentFromPath(
            StorageInterceptUtils::RemoveFirstComponentFromPath(remotePath));
    }
    return m_bIsDynamicContainer
        ? StorageInterceptUtils::RemoveFirstComponentFromPath(remotePath)
        : remotePath;
}
//---------------------------------------------------------------------------------------------------------------------
std::string SdkBlobClient::GetUrlSafeBase64(const std::string& uuid, off64_t blockId)
{
    // Fixed-length base64 block id (uuid + '-' padding + index); matches the legacy client so
    // uploads stay compatible.
    std::string idStr = std::to_string(blockId);
    idStr = uuid + std::string(48 - uuid.length() - idStr.length(), '-') + idStr;
    const std::vector<uint8_t> bytes(idStr.begin(), idStr.end());
    return Azure::Core::Convert::Base64Encode(bytes);
}
//---------------------------------------------------------------------------------------------------------------------
Blobs::StageBlockOptions SdkBlobClient::MakeStageBlockOptions() const
{
    Blobs::StageBlockOptions options;
    if (m_bUploadChecksumEnabled)
    {
        Blobs::TransferValidationOptions validation;
        validation.Algorithm = Azure::Storage::StorageChecksumAlgorithm::Crc64;
        options.ValidationOptions = validation;
    }
    return options;
}
//---------------------------------------------------------------------------------------------------------------------
bool SdkBlobClient::IsChecksumMismatch(const std::string& message)
{
    // The SDK structured-message decoder throws "checksum mismatch" only on a CRC64 failure.
    return message.find("checksum mismatch") != std::string::npos;
}
//---------------------------------------------------------------------------------------------------------------------
bool SdkBlobClient::ShouldRetryOnChecksumMismatch(
    const std::string& remotePath, off_t offset, size_t length,
    uint32_t attempt, uint32_t maxAttempts, const std::string& detail)
{
    // The SDK does NOT retry CRC64 mismatches: the response is 200 OK and the checksum is validated
    // later, during body read (StructuredMessageDecodingStream::OnRead throws), outside the HTTP retry
    // pipeline - so we re-download here.
    // Ref: https://github.com/Azure/azure-sdk-for-cpp/blob/azure-storage-blobs_12.18.0/sdk/storage/azure-storage-common/src/structured_message_decoding_stream.cpp
    if (!(m_bChecksumEnabled && IsChecksumMismatch(detail)))
    {
        return false;
    }
    if (attempt < maxAttempts)
    {
        const auto delay = storageretry::GetRetryIntervalExpJitter(
            attempt, m_blobRetryBaseDelay, m_blobRetryMaxDelay);
        LOG_WARN(
            "SdkBlobClient CRC64 mismatch, re-downloading (likely transient). "
            "path={} offset={} length={} attempt={}/{} backoff={}s detail={}",
            remotePath, offset, length, attempt, maxAttempts, delay.count(), detail);
        std::this_thread::sleep_for(delay);
        return true;
    }
    LOG_ERROR(
        "SdkBlobClient DATA INTEGRITY: CRC64 validation failed after {} attempts. "
        "path={} container={} blob={} offset={} length={} detail={}",
        maxAttempts, remotePath, GetContainerName(remotePath), GetBlobName(remotePath),
        offset, length, detail);
    throw RemoteClientException("BlobClientError: CRC64 mismatch: " + detail);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t SdkBlobClient::ReadChunkSync(const std::string& remotePath, void* buf, size_t length, off_t offset)
{
    Blobs::DownloadBlobOptions options;
    Azure::Core::Http::HttpRange range;
    range.Offset = static_cast<int64_t>(offset);
    range.Length = static_cast<int64_t>(length);
    options.Range = range;

    if (m_bChecksumEnabled)
    {
        // Request CRC64 (XSM) validation over the full range; the SDK throws on mismatch.
        Blobs::TransferValidationOptions validation;
        validation.Algorithm = Azure::Storage::StorageChecksumAlgorithm::Crc64;
        options.ValidationOptions = validation;
    }

    const uint32_t maxAttempts = m_bChecksumEnabled ? std::max(1u, m_nChecksumRetryMaxAttempts) : 1;

    for (uint32_t attempt = 1; attempt <= maxAttempts; ++attempt)
    {
        try
        {
            auto blobClient = GetContainerClient(remotePath).GetBlobClient(GetBlobName(remotePath));
            auto response = blobClient.Download(options);
            const size_t bytesRead = response.Value.BodyStream->ReadToCount(
                reinterpret_cast<uint8_t*>(buf), length);
            LOG_DEBUG("SdkBlobClient read chunk path={} nBytesRead={}", remotePath, bytesRead);
            return static_cast<ssize_t>(bytesRead);
        }
        catch (const Azure::Storage::StorageException& e)
        {
            // Reads with offset == blob-size return 416; treat as EOF (0 bytes), matching legacy client.
            if (e.StatusCode == Azure::Core::Http::HttpStatusCode::RangeNotSatisfiable)
            {
                LOG_DEBUG("Ignoring range-not-satisfiable for path={} offset={}", remotePath, offset);
                return 0;
            }
            if (ShouldRetryOnChecksumMismatch(remotePath, offset, length, attempt, maxAttempts, e.Message))
            {
                continue;
            }
            LOG_ERROR(
                "SdkBlobClient error reading path={} container={} blob={} status={} code={} msg={}",
                remotePath, GetContainerName(remotePath), GetBlobName(remotePath),
                static_cast<int>(e.StatusCode), e.ErrorCode, e.Message);
            throw RemoteClientException("BlobClientError: " + e.ErrorCode + ", " + e.Message);
        }
        catch (const std::exception& e)
        {
            if (ShouldRetryOnChecksumMismatch(remotePath, offset, length, attempt, maxAttempts, e.what()))
            {
                continue;
            }
            LOG_ERROR("SdkBlobClient unexpected error reading path={} what={}", remotePath, e.what());
            throw RemoteClientException(std::string("BlobClientError: ") + e.what());
        }
    }

    throw RemoteClientException("BlobClientError: checksum retry exhausted");
}
//---------------------------------------------------------------------------------------------------------------------
std::string SdkBlobClient::ReadDirectory(
    const std::string& remotePath,
    const std::string& continuationToken,
    std::vector<DirectoryEntry>& dirEntries)
{
    // Dynamic-container mount: ls on the mount point lists containers rather than blobs.
    if (m_bIsDynamicContainer && remotePath.empty())
    {
        Blobs::ListBlobContainersOptions options;
        if (!continuationToken.empty())
        {
            options.ContinuationToken = continuationToken;
        }
        options.PageSizeHint = static_cast<int32_t>(DIRENTS_BATCH_SIZE);

        try
        {
            auto response = GetServiceClient(remotePath)->ListBlobContainers(options);
            for (const auto& container : response.BlobContainers)
            {
                dirEntries.push_back(DirectoryEntry());
                dirEntries.back().d_ino = D_INO_VAL;
                dirEntries.back().d_off = dirEntries.size() - 1;
                dirEntries.back().d_reclen = sizeof(DirectoryEntry);
                dirEntries.back().d_type = DT_DIR;
                const size_t nameLen = std::min(container.Name.size(), static_cast<size_t>(D_NAME_LEN - 1));
                std::memcpy(dirEntries.back().d_name, container.Name.data(), nameLen);
                dirEntries.back().d_name[nameLen] = '\0';
                dirEntries.back().d_size = DIRECTORY_SIZE;
                dirEntries.back().d_atime = dirEntries.back().d_mtime = ToTimeT(container.Details.LastModified);
                dirEntries.back().d_version[0] = '\0';
            }
            return response.NextPageToken.HasValue() ? response.NextPageToken.Value() : "";
        }
        catch (const Azure::Storage::StorageException& e)
        {
            LOG_ERROR("SdkBlobClient failed to list containers path={} code={} msg={}",
                remotePath, e.ErrorCode, e.Message);
            throw RemoteClientException("BlobClientError: " + e.ErrorCode + ", " + e.Message);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("SdkBlobClient unexpected error listing containers path={} what={}", remotePath, e.what());
            throw RemoteClientException(std::string("BlobClientError: ") + e.what());
        }
    }

    const std::string blobPath = GetBlobName(remotePath);
    const std::string containerName = GetContainerName(remotePath);

    std::string prefix;
    if (!blobPath.empty())
    {
        prefix = (blobPath.back() == DIRECTORY_DELIMITER.back()) ? blobPath : blobPath + DIRECTORY_DELIMITER;
    }

    Blobs::ListBlobsOptions options;
    if (!prefix.empty())
    {
        options.Prefix = prefix;
    }
    if (!continuationToken.empty())
    {
        options.ContinuationToken = continuationToken;
    }
    options.PageSizeHint = static_cast<int32_t>(DIRENTS_BATCH_SIZE);
    // Request blob metadata so dacsfileversion is returned; without this the SDK omits metadata
    // and d_version falls back to the ETag (legacy cpplite always requested metadata on list).
    options.Include = Blobs::Models::ListBlobsIncludeFlags::Metadata;

    try
    {
        auto response = GetContainerClient(remotePath).ListBlobsByHierarchy(DIRECTORY_DELIMITER, options);

        for (const auto& blob : response.Blobs)
        {
            const std::string lastComponentName = ExtractLastComponent(blob.Name);

            dirEntries.push_back(DirectoryEntry());
            dirEntries.back().d_ino = D_INO_VAL;
            dirEntries.back().d_off = dirEntries.size() - 1;
            dirEntries.back().d_reclen = sizeof(DirectoryEntry);
            dirEntries.back().d_type = DT_REG;
            const size_t nameLen = std::min(lastComponentName.size(), static_cast<size_t>(D_NAME_LEN - 1));
            std::memcpy(dirEntries.back().d_name, lastComponentName.data(), nameLen);
            dirEntries.back().d_name[nameLen] = '\0';
            dirEntries.back().d_size = blob.BlobSize;
            dirEntries.back().d_atime = dirEntries.back().d_mtime = ToTimeT(blob.Details.LastModified);

            const std::string blobVersion = ExtractBlobVersion(blob);
            strncpy(dirEntries.back().d_version, blobVersion.c_str(), D_VERSION_LEN - 1);
            dirEntries.back().d_version[D_VERSION_LEN - 1] = '\0';
        }

        for (const auto& dirPrefix : response.BlobPrefixes)
        {
            const std::string lastComponentName = ExtractLastComponent(dirPrefix);

            dirEntries.push_back(DirectoryEntry());
            dirEntries.back().d_ino = D_INO_VAL;
            dirEntries.back().d_off = dirEntries.size() - 1;
            dirEntries.back().d_reclen = sizeof(DirectoryEntry);
            dirEntries.back().d_type = DT_DIR;
            const size_t nameLen = std::min(lastComponentName.size(), static_cast<size_t>(D_NAME_LEN - 1));
            std::memcpy(dirEntries.back().d_name, lastComponentName.data(), nameLen);
            dirEntries.back().d_name[nameLen] = '\0';
            dirEntries.back().d_size = DIRECTORY_SIZE;
            dirEntries.back().d_atime = dirEntries.back().d_mtime = time(nullptr);
            dirEntries.back().d_version[0] = '\0';
        }

        LOG_DEBUG("SdkBlobClient read directory path={} entriesAdded={} totalEntries={}",
            remotePath, response.Blobs.size() + response.BlobPrefixes.size(), dirEntries.size());
        return response.NextPageToken.HasValue() ? response.NextPageToken.Value() : "";
    }
    catch (const Azure::Storage::StorageException& e)
    {
        LOG_ERROR("SdkBlobClient failed to list directory path={} container={} code={} msg={}",
            remotePath, containerName, e.ErrorCode, e.Message);
        throw RemoteClientException("BlobClientError: " + e.ErrorCode + ", " + e.Message);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("SdkBlobClient unexpected error listing directory path={} what={}", remotePath, e.what());
        throw RemoteClientException(std::string("BlobClientError: ") + e.what());
    }
}
//---------------------------------------------------------------------------------------------------------------------
std::vector<SdkBlobClient::SdkBlobEntry> SdkBlobClient::ListOneBlob(
    const std::string& remotePath,
    const std::string& queryPrefix)
{
    std::vector<SdkBlobEntry> results;
    std::string continuationToken;

    do
    {
        Blobs::ListBlobsOptions options;
        if (!queryPrefix.empty())
        {
            options.Prefix = queryPrefix;
        }
        if (!continuationToken.empty())
        {
            options.ContinuationToken = continuationToken;
        }
        options.PageSizeHint = 1;
        // Request blob metadata so dacsfileversion is returned instead of falling back to the ETag.
        options.Include = Blobs::Models::ListBlobsIncludeFlags::Metadata;

        try
        {
            auto response = GetContainerClient(remotePath).ListBlobsByHierarchy(DIRECTORY_DELIMITER, options);

            if (!response.Blobs.empty())
            {
                const auto& blob = response.Blobs.front();
                SdkBlobEntry entry;
                entry.name = blob.Name;
                entry.isDirectory = false;
                entry.contentLength = blob.BlobSize;
                entry.lastModified = ToTimeT(blob.Details.LastModified);
                entry.version = ExtractBlobVersion(blob);
                results.push_back(std::move(entry));
                return results;
            }

            if (!response.BlobPrefixes.empty())
            {
                SdkBlobEntry entry;
                entry.name = response.BlobPrefixes.front();
                entry.isDirectory = true;
                results.push_back(std::move(entry));
                return results;
            }

            continuationToken = response.NextPageToken.HasValue() ? response.NextPageToken.Value() : "";
        }
        catch (const Azure::Storage::StorageException& e)
        {
            if (e.ErrorCode == "ContainerNotFound")
            {
                LOG_WARN("SdkBlobClient container not found for queryPrefix={}", queryPrefix);
                return results;
            }
            LOG_ERROR("SdkBlobClient list one blob failed queryPrefix={} code={} msg={}",
                queryPrefix, e.ErrorCode, e.Message);
            throw RemoteClientException("BlobClientError: " + e.ErrorCode + ", " + e.Message);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("SdkBlobClient unexpected error in list one blob queryPrefix={} what={}", queryPrefix, e.what());
            throw RemoteClientException(std::string("BlobClientError: ") + e.what());
        }
    } while (!continuationToken.empty());

    return results;
}
//---------------------------------------------------------------------------------------------------------------------
int SdkBlobClient::GetProperties(const std::string& remotePath, FileAttributes& attr)
{
    if (m_bIsDynamicContainer && remotePath.empty())
    {
        MakeDirectoryAttr(attr);
        return 0;
    }

    const std::string blobPath = GetBlobName(remotePath);
    const std::string containerName = GetContainerName(remotePath);

    if (blobPath.empty())
    {
        MakeDirectoryAttr(attr);
        return 0;
    }

    std::vector<SdkBlobEntry> listResult = ListOneBlob(remotePath, blobPath);

    if (blobPath.back() == DIRECTORY_DELIMITER.back())
    {
        if (!listResult.empty())
        {
            MakeDirectoryAttr(attr);
        }
        else
        {
            const std::string blobPathNoTrailing = blobPath.substr(0, blobPath.size() - 1);
            auto second = ListOneBlob(remotePath, blobPathNoTrailing);
            if (!second.empty() && second[0].name == blobPathNoTrailing)
            {
                errno = ENOTDIR;
                return -1;
            }
            errno = ENOENT;
            return -1;
        }
        return 0;
    }

    if (listResult.empty())
    {
        LOG_WARN("SdkBlobClient blob not found remotePath={} blobPath={}", remotePath, blobPath);
        errno = ENOENT;
        return -1;
    }

    const SdkBlobEntry& entry = listResult[0];
    if (entry.name == blobPath)
    {
        MakeFileAttr(entry, attr);
    }
    else if (entry.name == (blobPath + DIRECTORY_DELIMITER))
    {
        MakeDirectoryAttr(attr);
    }
    else
    {
        if (entry.name.rfind(blobPath, 0) != 0)
        {
            LOG_ERROR("SdkBlobClient list response mismatch response={} query={}", entry.name, blobPath);
            throw RemoteClientException("BlobClientError: Invalid blob path received in response");
        }

        const std::string blobPathWithTrailingDelimiter = blobPath + DIRECTORY_DELIMITER;
        bool directoryExists = false;
        if (entry.name < blobPathWithTrailingDelimiter)
        {
            auto second = ListOneBlob(remotePath, blobPathWithTrailingDelimiter);
            if (!second.empty())
            {
                directoryExists = true;
            }
        }

        if (directoryExists)
        {
            MakeDirectoryAttr(attr);
        }
        else
        {
            errno = ENOENT;
            return -1;
        }
    }

    LOG_DEBUG("SdkBlobClient GetProperties path={} isDir={} size={}", remotePath, attr.isDir, attr.size);
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
void SdkBlobClient::MakeDirectoryAttr(FileAttributes& attr)
{
    attr.isDir = true;
    attr.size = DIRECTORY_SIZE;
    timespec_get(&attr.mTime, TIME_UTC);
    attr.aTime = attr.cTime = attr.mTime;
}
//---------------------------------------------------------------------------------------------------------------------
void SdkBlobClient::MakeFileAttr(const SdkBlobEntry& entry, FileAttributes& attr)
{
    attr.isDir = false;
    attr.size = entry.contentLength;
    attr.mTime.tv_sec = entry.lastModified;
    attr.mTime.tv_nsec = 0;
    attr.aTime = attr.cTime = attr.mTime;
    attr.version = entry.version;
}
//---------------------------------------------------------------------------------------------------------------------
void SdkBlobClient::PrepareBlocksForUpload(const std::string& remotePath, size_t numBlocks)
{
    std::lock_guard<std::mutex> lock(m_lockBlockList);
    if (m_mapBlockList.find(remotePath) != m_mapBlockList.end())
    {
        throw RemoteClientException("BlobClientError: Block list already exists for path=" + remotePath);
    }

    auto [it, inserted] = m_mapBlockList.emplace(remotePath, std::vector<std::string>());
    it->second.reserve(numBlocks);

    const std::string uuid = Azure::Core::Uuid::CreateUuid().ToString();
    for (size_t blockId = 0; blockId < numBlocks; ++blockId)
    {
        it->second.emplace_back(GetUrlSafeBase64(uuid, static_cast<off64_t>(blockId)));
    }
}
//---------------------------------------------------------------------------------------------------------------------
int SdkBlobClient::StageBlocksForUpload(
    const std::string& remotePath,
    const void* buf,
    size_t length,
    off64_t blockId)
{
    std::string blockIdBase64;
    {
        std::unique_lock<std::mutex> lock(m_lockBlockList);
        auto it = m_mapBlockList.find(remotePath);
        if (it == m_mapBlockList.end())
        {
            throw RemoteClientException("BlobClientError: Block list not found for path=" + remotePath);
        }
        if (blockId < 0 || static_cast<size_t>(blockId) >= it->second.size())
        {
            throw RemoteClientException("BlobClientError: blockId out of range for path=" + remotePath);
        }
        blockIdBase64 = it->second[static_cast<size_t>(blockId)];
    }

    const uint32_t maxAttempts = m_bUploadChecksumEnabled ? std::max(1u, m_nChecksumRetryMaxAttempts) : 1;
    for (uint32_t attempt = 1; ; ++attempt)
    {
        try
        {
            auto blockBlobClient = GetContainerClient(remotePath).GetBlockBlobClient(GetBlobName(remotePath));
            Azure::Core::IO::MemoryBodyStream content(reinterpret_cast<const uint8_t*>(buf), length);
            const Blobs::StageBlockOptions options = MakeStageBlockOptions();
            auto stageResult = blockBlobClient.StageBlock(blockIdBase64, content, options);
            LOG_DEBUG("SdkBlobClient staged block path={} blockId={} length={} uploadChecksumEnabled={} attempt={} statusCode={}",
                remotePath, blockId, length, m_bUploadChecksumEnabled, attempt,
                static_cast<int>(stageResult.RawResponse->GetStatusCode()));
            return 0;
        }
        catch (const Azure::Storage::StorageException& e)
        {
            if (m_bUploadChecksumEnabled && e.StatusCode == Azure::Core::Http::HttpStatusCode::BadRequest
                && e.ErrorCode == "Crc64Mismatch")
            {
                if (attempt < maxAttempts)
                {
                    const auto backoff = storageretry::GetRetryIntervalExpJitter(
                        attempt, m_blobRetryBaseDelay, m_blobRetryMaxDelay);
                    LOG_WARN("SdkBlobClient upload CRC64 mismatch, re-staging block. "
                        "path={} blockId={} length={} attempt={}/{} backoff={}s code={} statusCode={} requestId={}",
                        remotePath, blockId, length, attempt, maxAttempts, backoff.count(),
                        e.ErrorCode, static_cast<int>(e.StatusCode), e.RequestId);
                    std::this_thread::sleep_for(backoff);
                    continue;
                }
                LOG_ERROR("SdkBlobClient DATA INTEGRITY: upload CRC64 validation failed after {} attempts. "
                    "path={} container={} blob={} blockId={} length={} code={} statusCode={} requestId={}",
                    maxAttempts, remotePath, GetContainerName(remotePath), GetBlobName(remotePath),
                    blockId, length, e.ErrorCode, static_cast<int>(e.StatusCode), e.RequestId);
                throw RemoteClientException("BlobClientError: upload CRC64 mismatch: " + e.ErrorCode);
            }
            LOG_ERROR("SdkBlobClient stage block failed path={} blockId={} code={} msg={}",
                remotePath, blockId, e.ErrorCode, e.Message);
            throw RemoteClientException("BlobClientError: " + e.ErrorCode + ", " + e.Message);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("SdkBlobClient unexpected error staging block path={} blockId={} what={}", remotePath, blockId, e.what());
            throw RemoteClientException(std::string("BlobClientError: ") + e.what());
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
int SdkBlobClient::CommitBlocksForUpload(const std::string& remotePath, const std::string& version)
{
    std::vector<std::string> blockList;
    {
        std::unique_lock<std::mutex> lock(m_lockBlockList);
        // If the block list is not found, we are uploading an empty blob
        auto it = m_mapBlockList.find(remotePath);
        if (it != m_mapBlockList.end())
        {
            blockList = std::move(it->second);
            m_mapBlockList.erase(it);
        }
    }

    Blobs::CommitBlockListOptions options;
    if (!version.empty())
    {
        options.Metadata[StorageInterceptConstants::BlobMetadataKeyVersion] = version;
    }

    try
    {
        auto blockBlobClient = GetContainerClient(remotePath).GetBlockBlobClient(GetBlobName(remotePath));
        blockBlobClient.CommitBlockList(blockList, options);
        LOG_INFO("SdkBlobClient committed blocks path={} nBlocks={}", remotePath, blockList.size());
        return 0;
    }
    catch (const Azure::Storage::StorageException& e)
    {
        LOG_ERROR("SdkBlobClient commit blocks failed path={} code={} msg={}",
            remotePath, e.ErrorCode, e.Message);
        throw RemoteClientException("BlobClientError: " + e.ErrorCode + ", " + e.Message);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("SdkBlobClient unexpected error committing blocks path={} what={}", remotePath, e.what());
        throw RemoteClientException(std::string("BlobClientError: ") + e.what());
    }
}
//---------------------------------------------------------------------------------------------------------------------
