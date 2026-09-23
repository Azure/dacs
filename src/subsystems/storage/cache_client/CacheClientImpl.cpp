/// Copyright (c) Microsoft Corporation.
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

#include "CacheClientImpl.h"
#include "CacheClient.h"
#include "CacheClientLogger.h"
#include "ChecksumFactory.h"
#include "utils/CacheClientRetry.h"
#include "CacheUtils.h"
#include "EncryptionUtil.h"
#include "LogUtils.h"
#include "StorageMetrics.h"
#include "cache.pb.h"

#include <boost/crc.hpp>
#include <boost/algorithm/string.hpp>

//---------------------------------------------------------------------------------------------------------------------
using namespace cacheutils;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
#define CACHECLIENT_EXTENDED_CONTEXT "cacheKey:{}| hostname:{}| connectionId:{}| "
#define LOG_DEBUG_CACHECLIENT_EXTENDED(message, request_object, ...) \
    LOG_DEBUG_REQID(CACHECLIENT_EXTENDED_CONTEXT message, request_object, \
    strCacheKey, hostname, serverConnection->GetId() __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO_CACHECLIENT_EXTENDED(message, request_object, ...) \
    LOG_INFO_REQID(CACHECLIENT_EXTENDED_CONTEXT message, request_object, \
    strCacheKey, hostname, serverConnection->GetId() __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN_CACHECLIENT_EXTENDED(message, request_object, ...) \
    LOG_WARN_REQID(CACHECLIENT_EXTENDED_CONTEXT message, request_object, \
    strCacheKey, hostname, serverConnection->GetId() __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERROR_CACHECLIENT_EXTENDED(message, request_object, ...) \
    LOG_ERROR_REQID(CACHECLIENT_EXTENDED_CONTEXT message, request_object, \
    strCacheKey, hostname, serverConnection->GetId() __VA_OPT__(,) __VA_ARGS__)
//---------------------------------------------------------------------------------------------------------------------
uint32_t CacheClientImpl::nDefaultConnectionPoolSize = 1024;
//---------------------------------------------------------------------------------------------------------------------
CacheClientImpl::CacheClientImpl()
: m_bInitialized{false}
, m_bEnableIB(false)
, m_bEnableCalculateChecksum(true)
, m_bEnableValidateChecksum(false)
, m_checksumMetadataPolicy(CHECKSUM_METADATA_OPTIONAL)
, m_checksumAlgorithm(DEFAULT_CHECKSUM_ALGORITHM)
, m_nConnectionPoolSize(0)
, m_nMaxBufferSizeBytes(0)
{
    LOG_INFO("CacheClientImpl constructor called");
}
//---------------------------------------------------------------------------------------------------------------------
CacheClientStatus CacheClientImpl::Initialize(
    /*[in]*/ const char* const* arrServers,
    /*[in]*/ const uint32_t nNumServers,
    /*[in]*/ const uint32_t nPort,
    /*[in]*/ const bool bEnableIB,
    /*[in]*/ const bool bEnableCalculateChecksum,
    /*[in]*/ const bool bEnableValidateChecksum,
    /*[in]*/ const char* strChecksumAlgorithm,
    /*[in]*/ const uint32_t nConnectionPoolSize,
    /*[in]*/ const bool bUseLocalServer,
    /*[in]*/ const bool bReuseConnections,
    /*[in]*/ const bool bIsCacheServerDiscoveryEnabled,
    /*[in]*/ const uint32_t nCacheServerDiscoveryIntervalInSeconds,
    /*[in]*/ const char* nCacheServerDiscoveryEndpoint,
    /*[in]*/ const bool bEnableConsistentHashing,
    /*[in]*/ CacheClientRetryConfig retryConfig,
    /*[in]*/ ChecksumMetadataPolicy checksumMetadataPolicy,
    /*[inopt]*/ std::size_t nMaxBufferSizeBytes,
    /*[inopt]*/ const int nSocketSndbufSize,
    /*[inopt]*/ const int nSocketRcvbufSize)
{
    std::lock_guard<std::mutex> lock(m_mtxInitialization);

    if(m_bInitialized)
    {
        LOG_ERROR("Cache client impl already initialized");
        return CacheClientStatus::INTERNAL_ERROR;
    }

    if(arrServers == nullptr && nNumServers != 0)
    {
        LOG_ERROR("Null server list provided with nonzero server count");
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(!bIsCacheServerDiscoveryEnabled && !bUseLocalServer && (arrServers == nullptr || nNumServers == 0))
    {
        LOG_ERROR("Invalid empty server list");
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(bIsCacheServerDiscoveryEnabled && (nCacheServerDiscoveryEndpoint == nullptr || strlen(nCacheServerDiscoveryEndpoint) == 0))
    {
        LOG_ERROR("Discovery enabled but no discovery endpoint provided");
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    // A zero interval would make the background refresher spin without ever sleeping.
    if(bIsCacheServerDiscoveryEnabled && nCacheServerDiscoveryIntervalInSeconds == 0)
    {
        LOG_ERROR("Discovery enabled but discovery interval is 0");
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(nPort == 0 || nPort > UINT16_MAX)
    {
        LOG_ERROR("Invalid port {}", nPort);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(checksumMetadataPolicy != CHECKSUM_METADATA_OPTIONAL &&
       checksumMetadataPolicy != CHECKSUM_METADATA_REQUIRED)
    {
        LOG_WARN(
            "Unknown checksum metadata policy {}; defaulting to optional",
            static_cast<int>(checksumMetadataPolicy));
        checksumMetadataPolicy = CHECKSUM_METADATA_OPTIONAL;
    }

    std::vector<std::string> vecServers;
    if(arrServers != nullptr)
    {
        for(uint32_t i = 0; i < nNumServers; i++)
        {
            if(arrServers[i] == nullptr)
            {
                LOG_ERROR("Invalid null server name");
                return CacheClientStatus::INVALID_ARGUMENT;
            }

            if(strlen(arrServers[i]) == 0)
            {
                LOG_ERROR("Invalid empty server name");
                return CacheClientStatus::INVALID_ARGUMENT;
            }

            vecServers.push_back(std::string(arrServers[i]));
        }
    }

    m_nMaxBufferSizeBytes = nMaxBufferSizeBytes;
    m_bEnableIB = bEnableIB;
    m_nConnectionPoolSize = nConnectionPoolSize == 0 ? nDefaultConnectionPoolSize : nConnectionPoolSize;
    m_bEnableCalculateChecksum = bEnableCalculateChecksum;
    m_bEnableValidateChecksum = bEnableValidateChecksum;
    m_checksumMetadataPolicy = checksumMetadataPolicy;

    if (!cacheclientretry::ValidateRetryConfig(retryConfig))
    {
        return CacheClientStatus::INVALID_ARGUMENT;
    }
    m_retryConfig = retryConfig;
    const std::string_view algorithmStr = strChecksumAlgorithm == nullptr ? std::string_view{} : strChecksumAlgorithm;
    
    m_checksumAlgorithm = ChecksumFactory::ParseAlgorithm(algorithmStr);
    if (m_checksumAlgorithm == ChecksumAlgorithm::INVALID)
    {
        m_checksumAlgorithm = DEFAULT_CHECKSUM_ALGORITHM;
        LOG_INFO("Invalid checksum algorithm provided, using default {}", ChecksumFactory::GetAlgorithmName(m_checksumAlgorithm));
    }
    
    // ConnectionManager's constructor can throw (std::runtime_error from the hasher factory, or a
    // bare `throw "No cache servers configured"`). Initialize is reached through an extern "C"
    // boundary, so letting an exception escape here terminates the host process instead of
    // surfacing a status the caller can act on.
    try
    {
        m_spConnectionManager = std::make_unique<ConnectionManager>(nPort,
                                                                    bUseLocalServer,
                                                                    vecServers,
                                                                    m_nConnectionPoolSize,
                                                                    bReuseConnections,
                                                                    bEnableIB,
                                                                    bIsCacheServerDiscoveryEnabled,
                                                                    nCacheServerDiscoveryIntervalInSeconds,
                                                                    nCacheServerDiscoveryEndpoint,
                                                                    bEnableConsistentHashing,
                                                                    nSocketSndbufSize,
                                                                    nSocketRcvbufSize);
    }
    catch(const std::exception& e)
    {
        LOG_ERROR("Failed to create the connection manager: {}", e.what());
        return CacheClientStatus::INTERNAL_ERROR;
    }
    catch(...)
    {
        LOG_ERROR("Failed to create the connection manager due to an unknown error");
        return CacheClientStatus::INTERNAL_ERROR;
    }
    
    m_bInitialized = true;
    STORAGE_METRIC_INITIALIZE();
    LOG_INFO("CacheClientImpl initialized. "
             "Enable Calculate checksum = {}, Enable Validate checksum = {}, Checksum algorithm = {}, "
             "Retry max attempts = {}, Retry base delay ms = {}, Retry max delay ms = {}",
             m_bEnableCalculateChecksum,
             m_bEnableValidateChecksum,
             ChecksumFactory::GetAlgorithmName(m_checksumAlgorithm),
             m_retryConfig.nMaxAttempts,
             m_retryConfig.nBaseDelayMillis,
             m_retryConfig.nMaxDelayMillis);
    return CacheClientStatus::SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
CacheClientStatus CacheClientImpl::Upload(
    /*[in]*/ const char* strCacheKey,
    /*[in]*/ const void* pBuffer,
    /*[in]*/ const std::size_t nBufferLength,
    /*[in]*/ const uint32_t nExpirySeconds,
    /*[in]*/ const char* strGroupId,
    /*[in]*/ const Metadata* pMetadata,
    /*[in]*/ const char* strEncryptionKey,
    /*[inopt]*/ bool bIgnoreLock,
    /*[inopt]*/ const char* strCorrelationId)
{
    std::string strRandId;
    if(strCorrelationId == nullptr)
    {
        strRandId = cacheutils::GetCorrelationId();
        strCorrelationId = strRandId.c_str();
    }

    if(!m_bInitialized)
    {
        LOG_ERROR_REQID_STR("Cache client impl not initialized", strCorrelationId);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    if(strCacheKey == nullptr)
    {
        LOG_ERROR_REQID_STR("Null cache key", strCorrelationId);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    const std::size_t cacheKeyLen = strlen(strCacheKey);
    if(cacheKeyLen == 0)
    {
        LOG_ERROR_REQID_STR("Empty cache key", strCorrelationId);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(cacheKeyLen > MAX_CACHE_KEY_LEN)
    {
        LOG_ERROR_REQID_STR(
            "Cache key {} is longer than max length {}", strCorrelationId, strCacheKey, MAX_CACHE_KEY_LEN);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(nBufferLength > m_nMaxBufferSizeBytes)
    {
        LOG_ERROR_REQID_STR("Upload size for cache-key {} is {} bytes, which is greater than max upload size {} bytes",
                            strCorrelationId,
                            strCacheKey,
                            nBufferLength,
                            m_nMaxBufferSizeBytes);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(pMetadata != nullptr && pMetadata->nKeyValuePairs > MAX_METADATA_KEY_VALUE_PAIRS)
    {
        LOG_ERROR_REQID_STR("Metadata count for cache-key {} is {}, which is greater than max metadata size {}",
                            strCorrelationId,
                            strCacheKey,
                            pMetadata ? pMetadata->nKeyValuePairs : 0,
                            MAX_METADATA_KEY_VALUE_PAIRS);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    STORAGE_METRIC_TIMER_START(cache_client_upload_latency_ms);
    STORAGE_METRIC_COUNTER_INCREMENT("cache_client_upload_requests");

    Request request;
    UploadRequest* uploadRequest = request.mutable_uploadrequest();
    uploadRequest->set_requestid(strCorrelationId);
    uploadRequest->set_filename(strCacheKey);
    uploadRequest->set_filesize(nBufferLength);
    uploadRequest->set_ignorelock(bIgnoreLock);

    LOG_DEBUG_REQID("Uploading file: {}, length: {}, metadata: {} ignoreLock: {}",
                    *uploadRequest,
                    strCacheKey,
                    nBufferLength,
                    pMetadata ? "yes" : "no",
                    bIgnoreLock);

    if(strGroupId != nullptr)
    {
        uploadRequest->set_groupid(strGroupId);
    }

    if(nExpirySeconds > 0)
    {
        uploadRequest->set_expiryseconds(nExpirySeconds);
    }

    if(pMetadata != nullptr || m_bEnableCalculateChecksum)
    {
        auto& fileMetadata = *(uploadRequest->mutable_metadata());
        if(pMetadata != nullptr)
        {
            for(uint32_t metadataIdx = 0; metadataIdx < pMetadata->nKeyValuePairs; metadataIdx++)
            {
                fileMetadata[pMetadata->arrKeyValuePairs[metadataIdx].strKey] =
                    pMetadata->arrKeyValuePairs[metadataIdx].strValue;
            }
        }

        if(m_bEnableCalculateChecksum)
        {
            auto checksumCalculator = ChecksumFactory::GetChecksumStrategyInstance(m_checksumAlgorithm);   
            if(checksumCalculator == nullptr)
            {
                LOG_ERROR_REQID(
                    "Failed to get checksum calculator, algorithm: {}",
                    *uploadRequest,
                    static_cast<int>(m_checksumAlgorithm));
                return CacheClientStatus::INTERNAL_ERROR;
            }
            const char emptyBuffer = '\0';
            const void* checksumBuffer = nBufferLength == 0 ? &emptyBuffer : pBuffer;
            uint32_t calculatedChecksum = checksumCalculator->CalculateChecksum(checksumBuffer, nBufferLength);
            std::string bufferChecksum = std::to_string(calculatedChecksum);
            fileMetadata[(char*)CACHE_META_CHUNK_CHECKSUM_KEY] = bufferChecksum;
            fileMetadata[(char*)CACHE_META_CHECKSUM_ALGORITHM_KEY] = std::to_string(static_cast<int>(m_checksumAlgorithm));
            LOG_DEBUG_REQID("Checksum calculated: {}, algorithm: {}", 
                           *uploadRequest, bufferChecksum, ChecksumFactory::GetAlgorithmName(m_checksumAlgorithm));
        }
    }

    EncryptionUtil encryptionUtil =
        EncryptionUtil(strEncryptionKey != nullptr /* isEncyptionEnabled */, strEncryptionKey);
    std::unique_ptr<char[]> encBuffer;
    int64_t encLen = -1;
    if(nBufferLength > 0)
    {
        if(!encryptionUtil.DoEncrypt(uploadRequest, (const char*)pBuffer, nBufferLength, encBuffer, encLen))
        {
            LOG_WARN_REQID("couldn't encrypt data to send to server, file: {}", *uploadRequest, strCacheKey);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        if(encLen >= 0 && static_cast<size_t>(encLen) == nBufferLength)
        {
            pBuffer = encBuffer.get();
        }
    }

    auto uploadAttempt = [&]() -> CacheClientStatus
    {
        const std::string hostname = m_spConnectionManager->GetCacheServerHostname(strCacheKey);
        if (hostname.empty())
        {
        LOG_DEBUG_REQID("No cache servers available for upload, key: {}. Cache discovery may be in progress.", *uploadRequest, strCacheKey);
            return CacheClientStatus::INTERNAL_ERROR;
        }
        LOG_DEBUG_REQID("Cache server hostname for file: {} is {}", *uploadRequest, strCacheKey, hostname);
        std::unique_ptr<IConnection> serverConnection = m_spConnectionManager->GetConnection(hostname);
        if(serverConnection.get() == nullptr)
        {
            LOG_WARN_REQID("couldn't get server connection, ignoring upload for file: {}", *uploadRequest, strCacheKey);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        bool bRet = false;
        if(nBufferLength > 0)
        {
            bRet = SendMessageToRemote(serverConnection.get(), request);
        }
        else
        {
            bRet = SendMessageToRemoteFinal(serverConnection.get(), request);
        }

        if(!bRet)
        {
            LOG_WARN_CACHECLIENT_EXTENDED("couldn't send upload request", *uploadRequest);
            m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        if(nBufferLength > 0 && !cacheutils::WriteAllFinal(serverConnection.get(), pBuffer, nBufferLength))
        {
            LOG_WARN_CACHECLIENT_EXTENDED("couldn't complete upload write", *uploadRequest);
            m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        std::string strResponse;
        if(!ReadMessageFromRemote(serverConnection.get(), strResponse))
        {
            LOG_WARN_CACHECLIENT_EXTENDED("couldn't read upload response", *uploadRequest);
            m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        UploadResponse response;
        if(!response.ParseFromString(strResponse))
        {
            LOG_WARN_CACHECLIENT_EXTENDED("couldn't parse upload response", *uploadRequest);
            m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        if(response.requestid().empty())
        {
            LOG_DEBUG_CACHECLIENT_EXTENDED(
                "Upload response request ID is unavailable; accepting for legacy compatibility",
                *uploadRequest);
        }
        else if(response.requestid() != uploadRequest->requestid())
        {
            LOG_ERROR_CACHECLIENT_EXTENDED(
                "Upload response request ID mismatch: requestedId={}, responseId={}",
                *uploadRequest,
                uploadRequest->requestid(),
                response.requestid());
            m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        CacheClientStatus status;
        if(response.result() == UploadResponse::SUCCESS)
        {
            LOG_DEBUG_CACHECLIENT_EXTENDED("Uploaded to server successfully", *uploadRequest);
            status = CacheClientStatus::SUCCESS;
        }
        else if(response.result() == UploadResponse::FILE_EXISTS)
        {
            LOG_WARN_CACHECLIENT_EXTENDED("Duplicate upload attempted", *uploadRequest);
            status = CacheClientStatus::KEY_EXISTS;
        }
        else
        {
            LOG_WARN_CACHECLIENT_EXTENDED(
                "Upload to server failed, error: {}",
                *uploadRequest,
                response.result());
            status = CacheClientStatus::INTERNAL_ERROR;
        }

        // We reuse connection only for success, because if the server has closed the connection
        // but another client operation reuses it, that operation will fail due to connection reset.
        m_spConnectionManager->ReleaseConnection(
            hostname,
            std::move(serverConnection),
            status != CacheClientStatus::SUCCESS /* discard connection on any error */);
        return status;
    };

    const CacheClientStatus status = cacheclientretry::AttemptWithRetry(
        uploadAttempt,
        [&](const cacheclientretry::CacheClientRetryLogContext& ctx)
        {
            LOG_WARN_REQID(
                "Upload attempt {}/{} for file={} returned status={}; is_retryable_error={} is_max_retry_exhausted={} will_retry={} delayMillis={}",
                *uploadRequest,
                ctx.failedAttempt,
                ctx.maxAttempts,
                strCacheKey,
                static_cast<int>(ctx.status),
                ctx.is_retryable_error,
                ctx.is_max_retry_exhausted,
                ctx.will_retry,
                (long)ctx.delay.count());
        },
        m_retryConfig);
    if(status == CacheClientStatus::SUCCESS)
    {
        STORAGE_METRIC_COUNTER_INCREMENT("cache_client_uploaded_bytes", nBufferLength);
        STORAGE_METRIC_TIMER_END(cache_client_upload_latency_ms, nBufferLength);
    }
    return status;
}
//---------------------------------------------------------------------------------------------------------------------
CacheClientStatus CacheClientImpl::Download(
    /*[in]*/ const char* strCacheKey,
    /*[out]*/ void* pOutBuffer,
    /*[inout]*/ std::size_t* nLength,
    /*[out]*/ Metadata* pMetadata,
    /*[in]*/ const char* strEncryptionKey,
    /*[inopt]*/ std::size_t nStartOffset,
    /*[inopt]*/ bool bEnableLock,
    /*[inopt]*/ const char* strCorrelationId)
{
    std::string strRandId;
    if(strCorrelationId == nullptr || *strCorrelationId == '\0')
    {
        strRandId = cacheutils::GetCorrelationId();
        strCorrelationId = strRandId.c_str();
    }

    if(!m_bInitialized)
    {
        LOG_ERROR_REQID_STR("Cache client impl not initialized", strCorrelationId);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    std::size_t cacheKeyLen = (strCacheKey == nullptr) ? 0 : strlen(strCacheKey);
    if(cacheKeyLen == 0)
    {
        LOG_ERROR_REQID_STR("Invalid or empty cache key", strCorrelationId);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(cacheKeyLen > MAX_CACHE_KEY_LEN)
    {
        LOG_ERROR_REQID_STR(
            "Cache key {} is longer than max length {}", strCorrelationId, strCacheKey, MAX_CACHE_KEY_LEN);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(*nLength > 0 && pOutBuffer == nullptr)
    {
        LOG_ERROR_REQID_STR("Null out buffer provided when size of buffer declared is {}", strCorrelationId, *nLength);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    EncryptionUtil encryptionUtil =
        EncryptionUtil(strEncryptionKey != nullptr /* isEncyptionEnabled */, strEncryptionKey);
    if(encryptionUtil.IsEncryptionEnabled() && nStartOffset != 0)
    {
        // TODO: This can be supported by decrypting a full remote chunk and then returning only the subset.
        //       This requires a buffer copy too.
        //       The interception client currently does not read partial chunks and hence we don't need this.
        LOG_ERROR_REQID_STR("Partial download not supported with encryption, key: {}", strCorrelationId, strCacheKey);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    STORAGE_METRIC_TIMER_START(cache_client_download_latency_ms);
    STORAGE_METRIC_COUNTER_INCREMENT("cache_client_download_requests");
    STORAGE_METRIC_COUNTER_INCREMENT("cache_client_download_requested_bytes", *nLength);

    CacheClientStatus cacheClientStatus = CacheClientStatus::INTERNAL_ERROR;
    Request request;
    DownloadRequest* downloadRequest = request.mutable_downloadrequest();
    downloadRequest->set_filename(strCacheKey);
    downloadRequest->set_requestid(strCorrelationId);
    downloadRequest->set_offset(nStartOffset);
    downloadRequest->set_length(*nLength);
    downloadRequest->set_enablelock(bEnableLock);
    LOG_DEBUG_REQID("Creating DownloadRequest filename: {}, offset: {}, length: {}, enablelock: {}",
                    *downloadRequest,
                    strCacheKey,
                    nStartOffset,
                    *nLength,
                    bEnableLock);

    const std::string hostname = m_spConnectionManager->GetCacheServerHostname(strCacheKey);
    if (hostname.empty())
    {
        LOG_DEBUG_REQID("No cache servers available for download, key: {}. Cache discovery may be in progress.", *downloadRequest, strCacheKey);
        return CacheClientStatus::INTERNAL_ERROR;
    }
    LOG_DEBUG_REQID("Cache server hostname for file: {} is {}", *downloadRequest, strCacheKey, hostname);

    std::unique_ptr<IConnection> serverConnection = m_spConnectionManager->GetConnection(hostname);
    if(!serverConnection.get())
    {
        LOG_WARN_REQID(
            "couldn't get server connection, key: {}, server: {}",
            *downloadRequest,
            strCacheKey,
            hostname);
        return cacheClientStatus;
    }
    bool discardConnection = false;

    if(!SendMessageToRemoteFinal(serverConnection.get(), request))
    {
        LOG_WARN_CACHECLIENT_EXTENDED(
            "couldn't send message to server",
            *downloadRequest);
        m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
        return cacheClientStatus;
    }

    std::string responseMessage;
    if(!ReadMessageFromRemote(serverConnection.get(), responseMessage))
    {
        LOG_WARN_CACHECLIENT_EXTENDED(
            "couldn't read message from server",
            *downloadRequest);
        m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
        return cacheClientStatus;
    }

    DownloadResponse response;
    if(!response.ParseFromString(responseMessage))
    {
        LOG_WARN_CACHECLIENT_EXTENDED(
            "Failed to parse response protobuf",
            *downloadRequest);
        m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
        return cacheClientStatus;
    }

    if(response.requestid().empty())
    {
        LOG_DEBUG_CACHECLIENT_EXTENDED(
            "Download response request ID is unavailable; accepting for legacy compatibility",
            *downloadRequest);
    }
    else if(response.requestid() != downloadRequest->requestid())
    {
        LOG_ERROR_CACHECLIENT_EXTENDED(
            "Download response request ID mismatch: requestedId={}, responseId={}",
            *downloadRequest,
            downloadRequest->requestid(),
            response.requestid());
        m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
        return cacheClientStatus;
    }

    LOG_DEBUG_CACHECLIENT_EXTENDED(
        "Download response: {}, filesize: {}, isFullDownload: {}",
        *downloadRequest,
        response.result(),
        response.filesize(),
        response.isfulldownload());

    std::size_t actualReadLen = 0;
    if(response.result() == DownloadResponse::SUCCESS)
    {
        actualReadLen = response.filesize();
        if(*nLength > 0 && actualReadLen > *nLength)
        {
            LOG_ERROR_CACHECLIENT_EXTENDED(
                "Received invalid response length: responseSize={}, requestedSize={}",
                *downloadRequest,
                actualReadLen,
                *nLength);
            m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
            return cacheClientStatus;
        }
        if(actualReadLen > 0)
        {
            if(!pOutBuffer)
            {
                LOG_ERROR_CACHECLIENT_EXTENDED(
                    "actualReadLen > 0 but no read buffer supplied",
                    *downloadRequest);
                m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
                return cacheClientStatus;
            }

            if (!encryptionUtil.DoDecrypt(
                    response, serverConnection.get(), static_cast<char*>(pOutBuffer), 
                    actualReadLen))
            {
                LOG_WARN_CACHECLIENT_EXTENDED(
                    "Failed to complete read from server, expectedBytes: {}",
                    *downloadRequest,
                    actualReadLen);
                m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
                return cacheClientStatus;
            }
        }

        if(m_bEnableValidateChecksum && response.isfulldownload())
        {
            LOG_DEBUG_CACHECLIENT_EXTENDED(
                "Validate Checksum enabled",
                *downloadRequest);
            auto& metadata = response.metadata();
            auto checksumIter = metadata.find(CACHE_META_CHUNK_CHECKSUM_KEY);

            if(checksumIter != metadata.end())
            {
                auto algorithmIter = metadata.find(CACHE_META_CHECKSUM_ALGORITHM_KEY);
                if(algorithmIter != metadata.end() && !algorithmIter->second.empty())
                {
                    try
                    {
                        int algorithmInt = std::stoi(algorithmIter->second);
                        if(algorithmInt < 0 || algorithmInt > UINT8_MAX)
                        {
                            throw std::out_of_range("Checksum algorithm exceeds enum range");
                        }
                        auto algorithm = static_cast<ChecksumAlgorithm>(algorithmInt);

                        auto checksumCalculator = ChecksumFactory::GetChecksumStrategyInstance(algorithm);   
                        if(checksumCalculator == nullptr)
                        {
                            throw std::invalid_argument("Unsupported checksum algorithm");
                        }
                        const char emptyBuffer = '\0';
                        const void* checksumBuffer = actualReadLen == 0 ? &emptyBuffer : pOutBuffer;
                        uint32_t calculatedChecksum =
                            checksumCalculator->CalculateChecksum(checksumBuffer, actualReadLen);
                        
                        uint64_t checksumValue = std::stoull(checksumIter->second);
                        if(checksumValue > UINT32_MAX)
                        {
                            throw std::out_of_range("Checksum exceeds uint32_t range");
                        }
                        uint32_t expectedChecksum = static_cast<uint32_t>(checksumValue);
                        LOG_DEBUG_CACHECLIENT_EXTENDED(
                            "Checksum calculated: algorithm: {}, expectedChecksum: {}, calculatedChecksum: {}",
                            *downloadRequest,
                            ChecksumFactory::GetAlgorithmName(algorithm),
                            expectedChecksum,
                            calculatedChecksum);
                        if(expectedChecksum != calculatedChecksum)
                        {
                            LOG_ERROR_CACHECLIENT_EXTENDED(
                                "Checksum mismatch: algorithm: {}, expectedChecksum: {}, calculatedChecksum: {}, "
                                "responseSize: {}, requestedSize: {}",
                                *downloadRequest,
                                ChecksumFactory::GetAlgorithmName(algorithm),
                                expectedChecksum,
                                calculatedChecksum,
                                actualReadLen,
                                *nLength);

                            cacheClientStatus = CacheClientStatus::CHECKSUM_MISMATCH;
                            m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
                            return cacheClientStatus;
                        }
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR_CACHECLIENT_EXTENDED(
                            "Exception during checksum validation: algorithm={}, expectedChecksum={}, error={}",
                            *downloadRequest,
                            algorithmIter->second,
                            checksumIter->second,
                            e.what());
                        cacheClientStatus = CacheClientStatus::CHECKSUM_MISMATCH;
                        m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
                        return cacheClientStatus;
                    }
                }
                else
                {
                    if(m_checksumMetadataPolicy == CHECKSUM_METADATA_REQUIRED)
                    {
                        LOG_ERROR_CACHECLIENT_EXTENDED(
                            "Checksum validation failed because algorithm metadata is missing",
                            *downloadRequest);
                        cacheClientStatus = CacheClientStatus::CHECKSUM_MISMATCH;
                        m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
                        return cacheClientStatus;
                    }
                    LOG_WARN_CACHECLIENT_EXTENDED(
                        "Checksum validation skipped because algorithm metadata is missing",
                        *downloadRequest);
                    discardConnection = true;
                }
            }
            else
            {
                if(m_checksumMetadataPolicy == CHECKSUM_METADATA_REQUIRED)
                {
                    LOG_ERROR_CACHECLIENT_EXTENDED(
                        "Checksum validation failed because checksum metadata is missing",
                        *downloadRequest);
                    cacheClientStatus = CacheClientStatus::CHECKSUM_MISMATCH;
                    m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), true);
                    return cacheClientStatus;
                }
                LOG_WARN_CACHECLIENT_EXTENDED(
                    "Checksum validation skipped because checksum metadata is missing",
                    *downloadRequest);
                discardConnection = true;
            }
        }
        if(pMetadata != nullptr)
        {
            pMetadata->nKeyValuePairs = static_cast<uint32_t>(response.metadata_size());
            pMetadata->arrKeyValuePairs = new KeyValuePair[pMetadata->nKeyValuePairs];

            uint32_t entryIndex = 0;
            for(auto& entry : response.metadata())
            {
                pMetadata->arrKeyValuePairs[entryIndex].strKey = strdup(entry.first.c_str());
                pMetadata->arrKeyValuePairs[entryIndex].strValue = strdup(entry.second.c_str());
                entryIndex++;
            }
        }

        *nLength = actualReadLen;
        cacheClientStatus = CacheClientStatus::SUCCESS;
        STORAGE_METRIC_COUNTER_INCREMENT("cache_client_download_cache_hits");
        STORAGE_METRIC_COUNTER_INCREMENT("cache_client_downloaded_bytes", actualReadLen);
        STORAGE_METRIC_TIMER_END(cache_client_download_latency_ms, actualReadLen);
    }
    else if(response.result() == DownloadResponse::NOT_FOUND_GOT_LOCK)
    {
        cacheClientStatus = CacheClientStatus::NOTFOUND_LOCKED;
        STORAGE_METRIC_COUNTER_INCREMENT("cache_client_download_cache_misses");
    }
    else if(response.result() == DownloadResponse::NOT_FOUND_ALREADY_LOCKED ||
            response.result() == DownloadResponse::NOT_FOUND)
    {
        cacheClientStatus = CacheClientStatus::NOTFOUND;
        STORAGE_METRIC_COUNTER_INCREMENT("cache_client_download_cache_misses");
    }
    else
    {
        LOG_ERROR_CACHECLIENT_EXTENDED("Server returned internal failure", *downloadRequest);
        cacheClientStatus = CacheClientStatus::INTERNAL_ERROR;
        discardConnection = true;
    }

    m_spConnectionManager->ReleaseConnection(hostname, std::move(serverConnection), discardConnection);
    return cacheClientStatus;
}
//---------------------------------------------------------------------------------------------------------------------
CacheClientStatus CacheClientImpl::DeleteGroup(/*[in]*/ const char* groupID,
                                               /*[inopt]*/ const char* strCorrelationId)
{
    std::string strRandId;
    if(strCorrelationId == nullptr)
    {
        strRandId = cacheutils::GetCorrelationId();
        strCorrelationId = strRandId.c_str();
    }

    if(!m_bInitialized)
    {
        LOG_ERROR_REQID_STR("Cache client impl not initialized", strCorrelationId);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    std::size_t groupIDLen = (groupID == nullptr) ? 0 : strlen(groupID);
    if(groupIDLen == 0)
    {
        LOG_ERROR_REQID_STR("Invalid or empty empty groupID", strCorrelationId);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(groupIDLen > MAX_GROUPID_LEN)
    {
        LOG_ERROR_REQID_STR("GroupID {} is longer than max length {}", strCorrelationId, groupID, MAX_GROUPID_LEN);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    STORAGE_METRIC_COUNTER_INCREMENT("cache_client_delete_group_requests");

    // Loop through each server and issue the delete request.
    // Always continue to all servers even if some fail
    //TODO: parallelize the delete requests to servers to speed up the process.
    //Also, consider sending the delete request to only the relevant server.
    const std::vector<std::string> cacheServerHostnames = m_spConnectionManager->GetAllCacheServerHostnames();
    for(const std::string& hostname : cacheServerHostnames)
    {
        auto status = DeleteGroupFromServer(hostname, groupID, strCorrelationId);
        if (status != CacheClientStatus::SUCCESS)
        {
            LOG_WARN_REQID_STR("DeleteGroup failed for server {}, continuing to next server", strCorrelationId, hostname);
        }
    }

    return CacheClientStatus::SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
CacheClientStatus CacheClientImpl::DeleteGroupFromServer(
    const std::string& hostname,
    const char* groupID,
    const char* strCorrelationId)
{
    Request request;
    DeleteRequest* deleteGroupRequest = request.mutable_deleterequest();
    deleteGroupRequest->set_requestid(strCorrelationId);
    deleteGroupRequest->set_groupid(groupID);

    std::unique_ptr<IConnection> connection = m_spConnectionManager->GetConnection(hostname);

    if(!connection || !connection.get())
    {
        LOG_ERROR_REQID("Couldn't get server connection {}, delete request for groupID: {}",
                        *deleteGroupRequest,
                        hostname,
                        groupID);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    if(!SendMessageToRemoteFinal(connection.get(), request))
    {
        LOG_ERROR_REQID("Couldn't send message to server {}, delete request for groupID: {}",
                        *deleteGroupRequest,
                        hostname,
                        groupID);
        m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    std::string requestMessage;
    if(!ReadMessageFromRemote(connection.get(), requestMessage))
    {
        LOG_ERROR_REQID("Couldn't read message from server {}, delete request for groupID: {}",
                        *deleteGroupRequest,
                        hostname,
                        groupID);
        m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    DeleteResponse response;
    if(!response.ParseFromString(requestMessage))
    {
        LOG_ERROR_REQID("Error: Failed to parse protobuf message from server {}, delete request for groupID: {}",
                        *deleteGroupRequest,
                        hostname,
                        groupID);
        m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    if(response.requestid().empty())
    {
        LOG_DEBUG_REQID(
            "Delete response request ID is unavailable; accepting for legacy compatibility: server={}, groupID={}",
            *deleteGroupRequest,
            hostname,
            groupID);
    }
    else if(response.requestid() != deleteGroupRequest->requestid())
    {
        LOG_ERROR_REQID(
            "Delete response request ID mismatch: server={}, requestedId={}, responseId={}, groupID={}",
            *deleteGroupRequest,
            hostname,
            deleteGroupRequest->requestid(),
            response.requestid(),
            groupID);
        m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    switch(response.result())
    {
    case DeleteResponse::SUCCESS:
        m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), false);
        return CacheClientStatus::SUCCESS;

    case DeleteResponse::INTERNAL_ERROR:
        LOG_ERROR_REQID(
            "Delete request failed due to internal error for server: {}", *deleteGroupRequest, hostname);
        m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
        return CacheClientStatus::INTERNAL_ERROR;

    default:
        LOG_ERROR_REQID("Delete request failed due to unknown response: {}, for server: {}",
                        *deleteGroupRequest,
                        response.result(),
                        hostname);
        m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
        return CacheClientStatus::INTERNAL_ERROR;
    }
}
//---------------------------------------------------------------------------------------------------------------------
CacheClientStatus CacheClientImpl::GetLastAccessTimeDistribution(
    /*[in]*/ uint64_t nDistributionBucketSizeInHours,
    /*[out]*/ LastAccessTimeDistribution* pLastAccessTimeDistribution,
    /*[inopt]*/ const char* strCorrelationId)
{
    std::string strRandId;
    if(strCorrelationId == nullptr)
    {
        strRandId = cacheutils::GetCorrelationId();
        strCorrelationId = strRandId.c_str();
    }

    if(!m_bInitialized)
    {
        LOG_ERROR_REQID_STR("Cache client impl not initialized", strCorrelationId);
        return CacheClientStatus::INTERNAL_ERROR;
    }


    if(nDistributionBucketSizeInHours <= 0)
    {
        LOG_ERROR_REQID_STR("Invalid distribution window", strCorrelationId);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(pLastAccessTimeDistribution == nullptr)
    {
        LOG_ERROR_REQID_STR("Null output parameter", strCorrelationId)
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    STORAGE_METRIC_COUNTER_INCREMENT("cache_client_get_last_access_time_distribution_requests");

    std::map<int64_t, uint64_t> lastAccessTimeDistribution;

    const std::vector<std::string> cacheServerHostnames = m_spConnectionManager->GetAllCacheServerHostnames();
    for(const std::string& hostname : cacheServerHostnames)
    {
        Request request;
        GetCacheLastAccessTimeDistributionRequest* lastAccessTimeDistributionRequest = request.mutable_getcachelastaccesstimedistributionrequest();
        lastAccessTimeDistributionRequest->set_requestid(strCorrelationId);
        lastAccessTimeDistributionRequest->set_distributionbucketsizeinhours(nDistributionBucketSizeInHours);

        std::unique_ptr<IConnection> connection = m_spConnectionManager->GetConnection(hostname);
        if(!connection || !connection.get())
        {
            LOG_ERROR_REQID("Couldn't get server connection, access time distribution request for server: {}",
                            *lastAccessTimeDistributionRequest,
                            hostname);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        if(!SendMessageToRemoteFinal(connection.get(), request))
        {
            LOG_ERROR_REQID("Couldn't send message to server, access time distribution request for server: {}",
                            *lastAccessTimeDistributionRequest,
                            hostname);
            m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        std::string requestMessage;
        if(!ReadMessageFromRemote(connection.get(), requestMessage))
        {
            LOG_ERROR_REQID("Couldn't read message from server, access time distribution request for server: {}",
                            *lastAccessTimeDistributionRequest,
                            hostname);
            m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        GetCacheLastAccessTimeDistributionResponse response;
        if(!response.ParseFromString(requestMessage))
        {
            LOG_ERROR_REQID("Error: Failed to parse protobuf message from server, access time distribution request for server: {}",
                            *lastAccessTimeDistributionRequest,
                            hostname);
            m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        if(response.requestid().empty())
        {
            LOG_DEBUG_REQID(
                "Last access time distribution response request ID is unavailable; "
                "accepting for legacy compatibility: server={}",
                *lastAccessTimeDistributionRequest,
                hostname);
        }
        else if(response.requestid() != lastAccessTimeDistributionRequest->requestid())
        {
            LOG_ERROR_REQID(
                "Last access time distribution response request ID mismatch: "
                "server={}, requestedId={}, responseId={}",
                *lastAccessTimeDistributionRequest,
                hostname,
                lastAccessTimeDistributionRequest->requestid(),
                response.requestid());
            m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        LOG_INFO_REQID("Last access time distribution response from server: {}, for server: {}",
                       *lastAccessTimeDistributionRequest,
                       response.result(),
                       hostname);

        switch (response.result())
        {
        case GetCacheLastAccessTimeDistributionResponse::SUCCESS:
            for (auto& entry : response.distributionmap())
            {
                lastAccessTimeDistribution[entry.first] += entry.second;
            }
            m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), false);
            continue;

        case GetCacheLastAccessTimeDistributionResponse::INTERNAL_ERROR:
            LOG_ERROR_REQID("Last access time distribution request failed due to internal error for server: {}",
                            *lastAccessTimeDistributionRequest,
                            hostname);
            return CacheClientStatus::INTERNAL_ERROR;

        default:
            LOG_ERROR_REQID("Last access time distribution request failed due to unknown response: {}, for server: {}",
                            *lastAccessTimeDistributionRequest,
                            response.result(),
                            hostname);
            return CacheClientStatus::INTERNAL_ERROR;
        }
    }

    pLastAccessTimeDistribution->nKeyValuePairs = lastAccessTimeDistribution.size();
    pLastAccessTimeDistribution->arrKeyValuePairs = new DistKeyValuePair[pLastAccessTimeDistribution->nKeyValuePairs];
    uint64_t entryIndex = 0;
    for(auto& entry : lastAccessTimeDistribution)
    {
        pLastAccessTimeDistribution->arrKeyValuePairs[entryIndex].nKey = entry.first;
        pLastAccessTimeDistribution->arrKeyValuePairs[entryIndex].nValue = entry.second;
        entryIndex++;
    }

    return CacheClientStatus::SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
CacheClientStatus CacheClientImpl::DeleteByLastAccessTime(/*[in]*/ const char* lastAccessTime,
                                               /*[inopt]*/ const char* strCorrelationId)
{
    std::string strRandId;
    if(strCorrelationId == nullptr)
    {
        strRandId = cacheutils::GetCorrelationId();
        strCorrelationId = strRandId.c_str();
    }

    if(!m_bInitialized)
    {
        LOG_ERROR_REQID_STR("Cache client impl not initialized", strCorrelationId);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    std::size_t lastAccessTimeLen = (lastAccessTime == nullptr) ? 0 : strlen(lastAccessTime);
    if(lastAccessTimeLen == 0)
    {
        LOG_ERROR_REQID_STR("Invalid or empty empty lastAccessTime", strCorrelationId);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(!cacheutils::IsValidLastAccessTime(lastAccessTime))
    {
        LOG_ERROR_REQID_STR("Invalid last access time format {}", strCorrelationId, lastAccessTime);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    STORAGE_METRIC_COUNTER_INCREMENT("cache_client_delete_by_last_access_time_requests");

    // Loop through each server, setup a connection and issue the delete request
    const std::vector<std::string> cacheServerHostnames = m_spConnectionManager->GetAllCacheServerHostnames();
    for(const std::string& hostname : cacheServerHostnames)
    {
        Request request;
        DeleteRequest* deleteByLastAccessTimeRequest = request.mutable_deleterequest();
        deleteByLastAccessTimeRequest->set_requestid(strCorrelationId);
        deleteByLastAccessTimeRequest->set_lastaccesstime(lastAccessTime);

        std::unique_ptr<IConnection> connection = m_spConnectionManager->GetConnection(hostname);

        if(!connection || !connection.get())
        {
            LOG_ERROR_REQID("Couldn't get server connection {}, delete by last access request for lastAccessTime: {}",
                            *deleteByLastAccessTimeRequest,
                            hostname,
                            lastAccessTime);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        if(!SendMessageToRemoteFinal(connection.get(), request))
        {
            LOG_ERROR_REQID("Couldn't send message to server {}, delete by last access request for lastAccessTime: {}",
                            *deleteByLastAccessTimeRequest,
                            hostname,
                            lastAccessTime);
            m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        std::string requestMessage;
        if(!ReadMessageFromRemote(connection.get(), requestMessage))
        {
            LOG_ERROR_REQID("Couldn't read message from server {}, delete by last access request for lastAccessTime: {}",
                            *deleteByLastAccessTimeRequest,
                            hostname,
                            lastAccessTime);
            m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        DeleteResponse response;
        if(!response.ParseFromString(requestMessage))
        {
            LOG_ERROR_REQID("Error: Failed to parse protobuf message from server {}, delete by last access request for lastAccessTime: {}",
                            *deleteByLastAccessTimeRequest,
                            hostname,
                            lastAccessTime);
            m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        if(response.requestid().empty())
        {
            LOG_DEBUG_REQID(
                "Delete by last access response request ID is unavailable; "
                "accepting for legacy compatibility: server={}, lastAccessTime={}",
                *deleteByLastAccessTimeRequest,
                hostname,
                lastAccessTime);
        }
        else if(response.requestid() != deleteByLastAccessTimeRequest->requestid())
        {
            LOG_ERROR_REQID(
                "Delete by last access response request ID mismatch: "
                "server={}, requestedId={}, responseId={}, lastAccessTime={}",
                *deleteByLastAccessTimeRequest,
                hostname,
                deleteByLastAccessTimeRequest->requestid(),
                response.requestid(),
                lastAccessTime);
            m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        LOG_INFO_REQID("Delete by last access request for time {} completed with response: {}, for server: {}",
                            *deleteByLastAccessTimeRequest,
                            lastAccessTime,
                            response.result(),
                            hostname);

        switch(response.result())
        {
        case DeleteResponse::SUCCESS:
            m_spConnectionManager->ReleaseConnection(hostname, std::move(connection), false);
            continue;

        case DeleteResponse::INTERNAL_ERROR:
            LOG_ERROR_REQID(
                "Delete by last access request failed due to internal error for server: {}", *deleteByLastAccessTimeRequest, hostname);
            return CacheClientStatus::INTERNAL_ERROR;

        default:
            LOG_ERROR_REQID("Delete by last access request failed due to unknown response: {}, for server: {}",
                            *deleteByLastAccessTimeRequest,
                            response.result(),
                            hostname);
            return CacheClientStatus::INTERNAL_ERROR;
        }
    }

    return CacheClientStatus::SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
bool CacheClientImpl::IsInitialized() const
{
    return m_bInitialized;
}
//----------------------------------------------------------------------------------------------------------------------