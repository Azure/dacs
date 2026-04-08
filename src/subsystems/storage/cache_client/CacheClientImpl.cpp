///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheClientImpl.h"
#include "CacheClient.h"
#include "CacheClientLogger.h"
#include "ChecksumFactory.h"
#include "utils/CacheClientRetry.h"
#include "CacheUtils.h"
#include "EncryptionUtil.h"
#include "LogUtils.h"
#include "cache.pb.h"

#include <boost/crc.hpp>
#include <boost/algorithm/string.hpp>

//---------------------------------------------------------------------------------------------------------------------
using namespace cacheutils;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
std::size_t CacheClientImpl::nDefaultConnectionPoolSize = 1024;
//---------------------------------------------------------------------------------------------------------------------
CacheClientImpl::CacheClientImpl()
: m_bInitialized{false}
, m_bEnableIB(false)
, m_bEnableAuth(false)
, m_bEnableCalculateChecksum(true)
, m_bEnableValidateChecksum(false)
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
    /*[in]*/ const bool bEnableAuth,
    /*[in]*/ const char* strAccountName,
    /*[in]*/ const char* strAccountKey,
    /*[in]*/ const bool bUseLocalServer,
    /*[in]*/ const bool bReuseConnections,
    /*[in]*/ const bool bIsCacheServerDiscoveryEnabled,
    /*[in]*/ const uint32_t nCacheServerDiscoveryIntervalInSeconds,
    /*[in]*/ const char* nCacheServerDiscoveryEndpoint,
    /*[in]*/ const bool bEnableConsistentHashing,
    /*[in]*/ CacheClientRetryConfig retryConfig,
    /*[inopt]*/ std::size_t nMaxBufferSizeBytes,
    /*[inopt]*/ const int nSocketSndbufSize,
    /*[inopt]*/ const int nSocketRcvbufSize)
{
    std::lock_guard<std::mutex> lock(m_mtxInitialization);

    if((strAccountName == nullptr || strAccountKey == nullptr) && bEnableAuth)
    {
        LOG_ERROR("Invalid empty account name or key when auth is enabled");
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(m_bInitialized)
    {
        LOG_ERROR("Cache client impl already initialized");
        return CacheClientStatus::INTERNAL_ERROR;
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

    if(nPort == 0 || nPort > UINT16_MAX)
    {
        LOG_ERROR("Invalid port {}", nPort);
        return CacheClientStatus::INVALID_ARGUMENT;
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
    m_bEnableAuth = bEnableAuth;
    m_bEnableCalculateChecksum = bEnableCalculateChecksum;
    m_bEnableValidateChecksum = bEnableValidateChecksum;

    if (!cacheclientretry::ValidateRetryConfig(retryConfig))
    {
        return CacheClientStatus::INVALID_ARGUMENT;
    }
    m_retryConfig = retryConfig;
    m_strAccountName = strAccountName == nullptr ? "" : std::string(strAccountName);
    m_strAccountKey = strAccountKey == nullptr ? "" : std::string(strAccountKey);
    std::string algorithmStr = (strChecksumAlgorithm == nullptr || strlen(strChecksumAlgorithm) == 0) ? 
        "" : std::string(strChecksumAlgorithm);
    
    m_checksumAlgorithm = ChecksumFactory::ParseAlgorithm(algorithmStr);
    if (m_checksumAlgorithm == ChecksumAlgorithm::INVALID)
    {
        m_checksumAlgorithm = DEFAULT_CHECKSUM_ALGORITHM;
        LOG_INFO("Invalid checksum algorithm provided, using default {}", ChecksumFactory::GetAlgorithmName(m_checksumAlgorithm));
    }
    
    m_spConnectionManager = std::make_unique<ConnectionManager>(nPort,
                                                                bUseLocalServer,
                                                                vecServers,
                                                                bEnableAuth,
                                                                m_strAccountName,
                                                                m_strAccountKey,
                                                                m_nConnectionPoolSize,
                                                                bReuseConnections,
                                                                bEnableIB,
                                                                bIsCacheServerDiscoveryEnabled,
                                                                nCacheServerDiscoveryIntervalInSeconds,
                                                                nCacheServerDiscoveryEndpoint,
                                                                bEnableConsistentHashing,
                                                                nSocketSndbufSize,
                                                                nSocketRcvbufSize);
    
    m_bInitialized = true;
    LOG_INFO("CacheClientImpl initialized. Enable Calculate checksum = {}, Enable Validate checksum = {}, Checksum algorithm = {}, Retry max attempts = {}, Retry base delay ms = {}, Retry max delay ms = {}",
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
            for(int metadataIdx = 0; metadataIdx < pMetadata->nKeyValuePairs; metadataIdx++)
            {
                fileMetadata[pMetadata->arrKeyValuePairs[metadataIdx].strKey] =
                    pMetadata->arrKeyValuePairs[metadataIdx].strValue;
            }
        }

        if (m_bEnableCalculateChecksum && nBufferLength > 0)
        {
            auto checksumCalculator = ChecksumFactory::GetChecksumStrategyInstance(m_checksumAlgorithm);   
            uint32_t calculatedChecksum = checksumCalculator->CalculateChecksum(pBuffer, nBufferLength);  
            std::string bufferChecksum = std::to_string(calculatedChecksum);
            fileMetadata[(char*)CACHE_META_CHUNK_CHECKSUM_KEY] = bufferChecksum;
            fileMetadata[(char*)CACHE_META_CHECKSUM_ALGORITHM_KEY] = std::to_string(static_cast<int>(m_checksumAlgorithm));
            LOG_DEBUG_REQID("Checksum calculated: {}, algorithm: {}", 
                           *uploadRequest, bufferChecksum, ChecksumFactory::GetAlgorithmName(m_checksumAlgorithm));
        }
    }

    if(m_bEnableAuth && !AddAuthParams(request, m_strAccountName, m_strAccountKey))
    {
        LOG_ERROR_REQID("Failed to create auth request for file: {}", *uploadRequest, strCacheKey);
        return CacheClientStatus::AUTH_FAILED;
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

        if(encLen == nBufferLength)
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
        std::unique_ptr<IConnection> serverConnection = move(m_spConnectionManager->GetConnection(hostname));
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
            LOG_WARN_REQID("couldn't send request, ignoring upload for file: {}", *uploadRequest, strCacheKey);
            m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        if(nBufferLength > 0 && !cacheutils::WriteAllFinal(serverConnection.get(), pBuffer, nBufferLength))
        {
            LOG_WARN_REQID("couldn't complete write to server, file: {}", *uploadRequest, strCacheKey);
            m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        std::string strResponse;
        if(!ReadMessageFromRemote(serverConnection.get(), strResponse))
        {
            LOG_WARN_REQID("couldn't read response from server. file: {}", *uploadRequest, strCacheKey);
            m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        UploadResponse response;
        if(!response.ParseFromString(strResponse))
        {
            LOG_WARN_REQID("couldn't parse response from server. file: {}", *uploadRequest, strCacheKey);
            m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        CacheClientStatus status;
        if(response.result() == UploadResponse::SUCCESS)
        {
            LOG_DEBUG_REQID("Uploaded to server successfully. file: {}", *uploadRequest, strCacheKey);
            status = CacheClientStatus::SUCCESS;
        }
        else if(response.result() == UploadResponse::FILE_EXISTS)
        {
            LOG_WARN_REQID("Duplicate upload attempted for file: {}", *uploadRequest, strCacheKey);
            status = CacheClientStatus::KEY_EXISTS;
        }
        else if(response.result() == UploadResponse::AUTH_FAILED)
        {
            LOG_ERROR_REQID("Authentication failed for file: {}", *uploadRequest, strCacheKey);
            status = CacheClientStatus::AUTH_FAILED;
        }
        else
        {
            LOG_WARN_REQID("Upload to server failed. file: {}, error: {}", *uploadRequest, strCacheKey, response.result());
            status = CacheClientStatus::INTERNAL_ERROR;
        }

        m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), false);
        return status;
    };

    return cacheclientretry::AttemptWithRetry(
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
    if(m_bEnableAuth && !AddAuthParams(request, m_strAccountName, m_strAccountKey))
    {
        LOG_WARN_REQID("failed to create auth request, key: {}", *downloadRequest, strCacheKey);
        return CacheClientStatus::AUTH_FAILED;
    }

    std::unique_ptr<IConnection> serverConnection = move(m_spConnectionManager->GetConnection(hostname));
    if(!serverConnection.get())
    {
        LOG_WARN_REQID("couldn't get server connection, key: {}", *downloadRequest, strCacheKey);
        return cacheClientStatus;
    }

    if(!SendMessageToRemoteFinal(serverConnection.get(), request))
    {
        LOG_WARN_REQID("couldn't send message to server, key: {}", *downloadRequest, strCacheKey);
        m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), true);
        return cacheClientStatus;
    }

    std::string responseMessage;
    if(!ReadMessageFromRemote(serverConnection.get(), responseMessage))
    {
        LOG_WARN_REQID("couldn't read message from server, key: {}", *downloadRequest, strCacheKey);
        m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), true);
        return cacheClientStatus;
    }

    DownloadResponse response;
    if(!response.ParseFromString(responseMessage))
    {
        LOG_WARN_REQID("error: Failed to parse protobuf message, key: {}", *downloadRequest, strCacheKey);
        m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), true);
        return cacheClientStatus;
    }

    LOG_DEBUG_REQID("Download response from server: {}, key: {}, filesize: {}, isFullDownload: {}",
                    *downloadRequest,
                    response.result(),
                    strCacheKey,
                    response.filesize(),
                    response.isfulldownload());

    std::size_t actualReadLen = -1;
    if(response.result() == DownloadResponse::SUCCESS)
    {
        actualReadLen = response.filesize();
        if(*nLength > 0 && actualReadLen > *nLength)
        {
            LOG_WARN_REQID("Received invalid length from server, key: {}, got: {}, expected: {}",
                           *downloadRequest,
                           strCacheKey,
                           actualReadLen,
                           *nLength);
            m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), true);
            return cacheClientStatus;
        }

        if(actualReadLen > 0)
        {
            if(!pOutBuffer)
            {
                LOG_ERROR_REQID(
                    "actualReadLen > 0 but no read buffer supplied, key: {}", *downloadRequest, strCacheKey);
                m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), true);
                return cacheClientStatus;
            }

            if (!encryptionUtil.DoDecrypt(
                    response, serverConnection.get(), static_cast<char*>(pOutBuffer), 
                    actualReadLen))
            {
                LOG_WARN_REQID("Failed to complete read from server, key: {}", *downloadRequest, strCacheKey);
                m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), true);
                return cacheClientStatus;
            }
        }

        if(m_bEnableValidateChecksum && response.isfulldownload())
        {
            LOG_DEBUG_REQID("Validate Checksum enabled, key: {}", *downloadRequest, strCacheKey);
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
                        auto algorithm = static_cast<ChecksumAlgorithm>(algorithmInt);

                        auto checksumCalculator = ChecksumFactory::GetChecksumStrategyInstance(algorithm);   
                        uint32_t calculatedChecksum = checksumCalculator->CalculateChecksum(pOutBuffer, actualReadLen);             
                        
                        uint32_t receivedChecksum = std::stoul(checksumIter->second);
                        LOG_DEBUG_REQID("Checksum for file: {}, algorithm: {}, receivedChecksum: {}, calculatedChecksum: {}",
                                        *downloadRequest,
                                        strCacheKey,
                                        ChecksumFactory::GetAlgorithmName(algorithm),
                                        receivedChecksum,
                                        calculatedChecksum);
                        if(receivedChecksum != calculatedChecksum)
                        {
                            LOG_ERROR_REQID("Checksum mismatch, key: {}, algorithm: {}, received: {}, calculated: {}",
                                           *downloadRequest,
                                           strCacheKey,
                                           ChecksumFactory::GetAlgorithmName(algorithm),
                                           receivedChecksum,
                                           calculatedChecksum);

                            cacheClientStatus = CacheClientStatus::CHECKSUM_MISMATCH;
                            m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), false);
                            return cacheClientStatus;
                        }
                    }
                    catch (const std::exception& e)
                    {
                        LOG_WARN_REQID("Failed to parse algorithm enum from metadata: {}, key: {}", 
                                      *downloadRequest, algorithmIter->second, strCacheKey);
                    }
                }
                else
                {
                    LOG_DEBUG_REQID("No algorithm in metadata, skipping checksum validation, key: {}", *downloadRequest, strCacheKey);
                }
            }
            else
            {
                LOG_DEBUG_REQID("Checksum not found in metadata, skipping validation, key: {}", *downloadRequest, strCacheKey);
            }
        }

        if(pMetadata != nullptr)
        {
            pMetadata->nKeyValuePairs = response.metadata_size();
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
    }
    else if(response.result() == DownloadResponse::NOT_FOUND_GOT_LOCK)
    {
        cacheClientStatus = CacheClientStatus::NOTFOUND_LOCKED;
    }
    else if(response.result() == DownloadResponse::NOT_FOUND_ALREADY_LOCKED ||
            response.result() == DownloadResponse::NOT_FOUND)
    {
        cacheClientStatus = CacheClientStatus::NOTFOUND;
    }
    else if(response.result() == DownloadResponse::AUTH_FAILED)
    {
        cacheClientStatus = CacheClientStatus::AUTH_FAILED;
    }
    else
    {
        LOG_ERROR_REQID("Server returned internal failure for key: {}", *downloadRequest, strCacheKey);
        cacheClientStatus = CacheClientStatus::INTERNAL_ERROR;
    }

    m_spConnectionManager->ReleaseConnection(hostname, move(serverConnection), false);
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

    // Loop through each server, setup a connection and issue the delete request
    const std::vector<std::string> cacheServerHostnames = m_spConnectionManager->GetAllCacheServerHostnames();
    for(const std::string& hostname : cacheServerHostnames)
    {
        Request request;
        DeleteRequest* deleteGroupRequest = request.mutable_deleterequest();
        deleteGroupRequest->set_requestid(strCorrelationId);
        deleteGroupRequest->set_groupid(groupID);

        if(m_bEnableAuth && !AddAuthParams(request, m_strAccountName, m_strAccountKey))
        {
            LOG_ERROR_REQID(
                "Failed to set auth for delete group request for group ID: {}", *deleteGroupRequest, groupID);
            return CacheClientStatus::AUTH_FAILED;
        }

        std::unique_ptr<IConnection> connection = move(m_spConnectionManager->GetConnection(hostname));

        if(!connection || !connection.get())
        {
            LOG_ERROR_REQID("Couldn't get server connection {}, delete request for groupID: {}",
                            *deleteGroupRequest,
                            hostname,
                            groupID);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        if(!SendMessageToRemote(connection.get(), request))
        {
            LOG_ERROR_REQID("Couldn't send message to server {}, delete request for groupID: {}",
                            *deleteGroupRequest,
                            hostname,
                            groupID);
            m_spConnectionManager->ReleaseConnection(hostname, move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        std::string requestMessage;
        if(!ReadMessageFromRemote(connection.get(), requestMessage))
        {
            LOG_ERROR_REQID("Couldn't read message from server {}, delete request for groupID: {}",
                            *deleteGroupRequest,
                            hostname,
                            groupID);
            m_spConnectionManager->ReleaseConnection(hostname, move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        DeleteResponse response;
        if(!response.ParseFromString(requestMessage))
        {
            LOG_ERROR_REQID("Error: Failed to parse protobuf message from server {}, delete request for groupID: {}",
                            *deleteGroupRequest,
                            hostname,
                            groupID);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        switch(response.result())
        {
        case DeleteResponse::SUCCESS:
            m_spConnectionManager->ReleaseConnection(hostname, move(connection), false);
            continue;

        case DeleteResponse::AUTH_FAILED:
            LOG_ERROR_REQID("Delete request failed due to auth failure for server: {}", *deleteGroupRequest, hostname);
            return CacheClientStatus::AUTH_FAILED;

        case DeleteResponse::INTERNAL_ERROR:
            LOG_ERROR_REQID(
                "Delete request failed due to internal error for server: {}", *deleteGroupRequest, hostname);
            return CacheClientStatus::INTERNAL_ERROR;

        default:
            LOG_ERROR_REQID("Delete request failed due to unknown response: {}, for server: {}",
                            *deleteGroupRequest,
                            response.result(),
                            hostname);
            return CacheClientStatus::INTERNAL_ERROR;
        }
    }

    return CacheClientStatus::SUCCESS;
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

    std::map<int64_t, uint64_t> lastAccessTimeDistribution;

    const std::vector<std::string> cacheServerHostnames = m_spConnectionManager->GetAllCacheServerHostnames();
    for(const std::string& hostname : cacheServerHostnames)
    {
        Request request;
        GetCacheLastAccessTimeDistributionRequest* lastAccessTimeDistributionRequest = request.mutable_getcachelastaccesstimedistributionrequest();
        lastAccessTimeDistributionRequest->set_requestid(strCorrelationId);
        lastAccessTimeDistributionRequest->set_distributionbucketsizeinhours(nDistributionBucketSizeInHours);

        if(m_bEnableAuth && !AddAuthParams(request, m_strAccountName, m_strAccountKey))
        {
            LOG_ERROR_REQID("Failed to set auth for Last access time distribution request for server: {}",
                            *lastAccessTimeDistributionRequest,
                            hostname);
            return CacheClientStatus::AUTH_FAILED;
        }

        std::unique_ptr<IConnection> connection = move(m_spConnectionManager->GetConnection(hostname));
        if(!connection || !connection.get())
        {
            LOG_ERROR_REQID("Couldn't get server connection, access time distribution request for server: {}",
                            *lastAccessTimeDistributionRequest,
                            hostname);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        if(!SendMessageToRemote(connection.get(), request))
        {
            LOG_ERROR_REQID("Couldn't send message to server, access time distribution request for server: {}",
                            *lastAccessTimeDistributionRequest,
                            hostname);
            m_spConnectionManager->ReleaseConnection(hostname, move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        std::string requestMessage;
        if(!ReadMessageFromRemote(connection.get(), requestMessage))
        {
            LOG_ERROR_REQID("Couldn't read message from server, access time distribution request for server: {}",
                            *lastAccessTimeDistributionRequest,
                            hostname);
            m_spConnectionManager->ReleaseConnection(hostname, move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        GetCacheLastAccessTimeDistributionResponse response;
        if(!response.ParseFromString(requestMessage))
        {
            LOG_ERROR_REQID("Error: Failed to parse protobuf message from server, access time distribution request for server: {}",
                            *lastAccessTimeDistributionRequest,
                            hostname);
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
            m_spConnectionManager->ReleaseConnection(hostname, move(connection), false);
            continue;

        case GetCacheLastAccessTimeDistributionResponse::AUTH_FAILED:
            LOG_ERROR_REQID("Last access time distribution request failed due to auth failure for server: {}",
                            *lastAccessTimeDistributionRequest,
                            hostname);
            return CacheClientStatus::AUTH_FAILED;

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

    // Loop through each server, setup a connection and issue the delete request
    const std::vector<std::string> cacheServerHostnames = m_spConnectionManager->GetAllCacheServerHostnames();
    for(const std::string& hostname : cacheServerHostnames)
    {
        Request request;
        DeleteRequest* deleteByLastAccessTimeRequest = request.mutable_deleterequest();
        deleteByLastAccessTimeRequest->set_requestid(strCorrelationId);
        deleteByLastAccessTimeRequest->set_lastaccesstime(lastAccessTime);

        if(m_bEnableAuth && !AddAuthParams(request, m_strAccountName, m_strAccountKey))
        {
            LOG_ERROR_REQID(
                "Failed to set auth for delete by last access request for lastAccessTime: {}", *deleteByLastAccessTimeRequest, lastAccessTime);
            return CacheClientStatus::AUTH_FAILED;
        }

        std::unique_ptr<IConnection> connection = move(m_spConnectionManager->GetConnection(hostname));

        if(!connection || !connection.get())
        {
            LOG_ERROR_REQID("Couldn't get server connection {}, delete by last access request for lastAccessTime: {}",
                            *deleteByLastAccessTimeRequest,
                            hostname,
                            lastAccessTime);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        if(!SendMessageToRemote(connection.get(), request))
        {
            LOG_ERROR_REQID("Couldn't send message to server {}, delete by last access request for lastAccessTime: {}",
                            *deleteByLastAccessTimeRequest,
                            hostname,
                            lastAccessTime);
            m_spConnectionManager->ReleaseConnection(hostname, move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        std::string requestMessage;
        if(!ReadMessageFromRemote(connection.get(), requestMessage))
        {
            LOG_ERROR_REQID("Couldn't read message from server {}, delete by last access request for lastAccessTime: {}",
                            *deleteByLastAccessTimeRequest,
                            hostname,
                            lastAccessTime);
            m_spConnectionManager->ReleaseConnection(hostname, move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        DeleteResponse response;
        if(!response.ParseFromString(requestMessage))
        {
            LOG_ERROR_REQID("Error: Failed to parse protobuf message from server {}, delete by last access request for lastAccessTime: {}",
                            *deleteByLastAccessTimeRequest,
                            hostname,
                            lastAccessTime);
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
            m_spConnectionManager->ReleaseConnection(hostname, move(connection), false);
            continue;

        case DeleteResponse::AUTH_FAILED:
            LOG_ERROR_REQID("Delete by last access request failed due to auth failure for server: {}", *deleteByLastAccessTimeRequest, hostname);
            return CacheClientStatus::AUTH_FAILED;

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