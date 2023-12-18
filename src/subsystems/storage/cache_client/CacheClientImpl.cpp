///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheClientImpl.h"

#include "CacheClient.h"
#include "CacheClientLogger.h"
#include "CacheUtils.h"
#include "EncryptionUtil.h"
#include "LogUtils.h"
#include "cache.pb.h"

#include <boost/crc.hpp>

//---------------------------------------------------------------------------------------------------------------------
using namespace cacheutils;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
std::size_t CacheClientImpl::nDefaultConnectionPoolSize = 1024;
//---------------------------------------------------------------------------------------------------------------------
CacheClientImpl::CacheClientImpl() : m_bInitialized{false}
{
    LOG_INFO("CacheClientImpl constructor called");
}
//---------------------------------------------------------------------------------------------------------------------
CacheClientStatus CacheClientImpl::Initialize(
    /*[in]*/ const char* const* arrServers,
    /*[in]*/ uint32_t nNumServers,
    /*[in]*/ uint32_t nPort,
    /*[in]*/ bool bEnableIB,
    /*[in]*/ bool bEnableCalculateChecksum,
    /*[in]*/ bool bEnableValidateChecksum,
    /*[in]*/ uint32_t nConnectionPoolSize,
    /*[in]*/ bool bEnableAuth,
    /*[in]*/ const char* strAccountName,
    /*[in]*/ const char* strAccountKey,
    /*[inopt]*/ bool bUseLocalServer,
    /*[inopt]*/ bool bReuseConnections,
    /*[inopt]*/ std::size_t nMaxBufferSizeBytes)
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

    if(!bUseLocalServer && (arrServers == nullptr || nNumServers == 0))
    {
        LOG_ERROR("Invalid empty server list");
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    if(nPort == 0 || nPort > UINT16_MAX)
    {
        LOG_ERROR("Invalid port {}", nPort);
        return CacheClientStatus::INVALID_ARGUMENT;
    }

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

            m_vecServers.push_back(std::string(arrServers[i]));
        }
    }

    m_nNumServers = nNumServers;
    m_nMaxBufferSizeBytes = nMaxBufferSizeBytes;
    m_nPort = nPort;
    m_bEnableIB = bEnableIB;
    m_nConnectionPoolSize = nConnectionPoolSize == 0 ? nDefaultConnectionPoolSize : nConnectionPoolSize;
    m_bEnableAuth = bEnableAuth;
    m_bEnableCalculateChecksum = bEnableCalculateChecksum;
    m_bEnableValidateChecksum = bEnableValidateChecksum;
    m_strAccountName = strAccountName == nullptr ? "" : std::string(strAccountName);
    m_strAccountKey = strAccountKey == nullptr ? "" : std::string(strAccountKey);
    m_spConnectionManager = std::make_unique<ConnectionManager>(nPort,
                                                                bUseLocalServer,
                                                                m_vecServers,
                                                                bEnableAuth,
                                                                m_strAccountName,
                                                                m_strAccountKey,
                                                                nConnectionPoolSize,
                                                                bReuseConnections,
                                                                bEnableIB);
    m_bInitialized = true;
    LOG_INFO("CacheClientImpl initialized. Num servers = {}. Port = {}. Enable IB = {}. Connection pool size = {}, Enable Calculate checksum = {}, Enable Validate checksum = {}",
             nNumServers,
             nPort,
             bEnableIB,
             m_nConnectionPoolSize,
             m_bEnableCalculateChecksum,
             m_bEnableValidateChecksum);
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

    // TODO: Implement retries on failure
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
            std::string bufferChecksum = std::to_string(CalculateChecksum(pBuffer, nBufferLength));

            fileMetadata[(char*)CACHE_META_CHUNK_CHECKSUM_KEY] = bufferChecksum;
            LOG_DEBUG_REQID("Checksum for file: {}, checksum: {}", *uploadRequest, strCacheKey, bufferChecksum);
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

        // encLen will be updated if encrypted data needs to be used
        if(encLen == nBufferLength)
        {
            // Data encrypted to new buffer, replace original send buffer with the encrypted one
            pBuffer = encBuffer.get();
        }
    }

    const uint32_t nServerIndex = GetServerIndex(strCacheKey, m_nNumServers);
    std::unique_ptr<IConnection> serverConnection = move(m_spConnectionManager->GetConnection(nServerIndex));
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
        m_spConnectionManager->ReleaseConnection(nServerIndex, move(serverConnection), true);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    if(nBufferLength > 0 && !cacheutils::WriteAllFinal(serverConnection.get(), pBuffer, nBufferLength))
    {
        LOG_WARN_REQID("couldn't complete write to server, file: {}", *uploadRequest, strCacheKey);
        m_spConnectionManager->ReleaseConnection(nServerIndex, move(serverConnection), true);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    std::string strResponse;
    if(!ReadMessageFromRemote(serverConnection.get(), strResponse))
    {
        LOG_WARN_REQID("couldn't read response from server. file: {}", *uploadRequest, strCacheKey);
        m_spConnectionManager->ReleaseConnection(nServerIndex, move(serverConnection), true);
        return CacheClientStatus::INTERNAL_ERROR;
    }

    UploadResponse response;
    if(!response.ParseFromString(strResponse))
    {
        LOG_WARN_REQID("couldn't parse response from server. file: {}", *uploadRequest, strCacheKey);
        m_spConnectionManager->ReleaseConnection(nServerIndex, move(serverConnection), true);
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
        return CacheClientStatus::AUTH_FAILED;
    }
    else
    {
        LOG_WARN_REQID("Upload to server failed. file: {}, error: {}", *uploadRequest, strCacheKey, response.result());
        status = CacheClientStatus::INTERNAL_ERROR;
    }

    m_spConnectionManager->ReleaseConnection(nServerIndex, move(serverConnection), false);
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

    if(m_bEnableAuth && !AddAuthParams(request, m_strAccountName, m_strAccountKey))
    {
        LOG_WARN_REQID("failed to create auth request, key: {}", *downloadRequest, strCacheKey);
        return CacheClientStatus::AUTH_FAILED;
    }

    const uint32_t serverIndex = GetServerIndex(strCacheKey, m_nNumServers);
    std::unique_ptr<IConnection> serverConnection = move(m_spConnectionManager->GetConnection(serverIndex));
    if(!serverConnection.get())
    {
        LOG_WARN_REQID("couldn't get server connection, key: {}", *downloadRequest, strCacheKey);
        return cacheClientStatus;
    }

    if(!SendMessageToRemoteFinal(serverConnection.get(), request))
    {
        LOG_WARN_REQID("couldn't send message to server, key: {}", *downloadRequest, strCacheKey);
        m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
        return cacheClientStatus;
    }

    std::string responseMessage;
    if(!ReadMessageFromRemote(serverConnection.get(), responseMessage))
    {
        LOG_WARN_REQID("couldn't read message from server, key: {}", *downloadRequest, strCacheKey);
        m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
        return cacheClientStatus;
    }

    DownloadResponse response;
    if(!response.ParseFromString(responseMessage))
    {
        LOG_WARN_REQID("error: Failed to parse protobuf message, key: {}", *downloadRequest, strCacheKey);
        m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
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
            m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
            return cacheClientStatus;
        }

        if(actualReadLen > 0)
        {
            if(!pOutBuffer)
            {
                LOG_ERROR_REQID(
                    "actualReadLen > 0 but no read buffer supplied, key: {}", *downloadRequest, strCacheKey);
                m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
                return cacheClientStatus;
            }

            if(!encryptionUtil.DoDecrypt(
                   response, serverConnection.get(), static_cast<char*>(pOutBuffer), actualReadLen))
            {
                LOG_WARN_REQID("Failed to complete read from server, key: {}", *downloadRequest, strCacheKey);
                m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
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
                uint32_t calculatedChecksum = CalculateChecksum(pOutBuffer, actualReadLen);
                
                uint32_t receivedChecksum = std::stoul(checksumIter->second);
                LOG_DEBUG_REQID("Checksum for file: {}, receivedChecksum: {}, calculatedChecksum {}",
                                *downloadRequest,
                                strCacheKey,
                                receivedChecksum,
                                calculatedChecksum);
                if(receivedChecksum != calculatedChecksum)
                {
                    LOG_ERROR_REQID("Checksum mismatch, key: {}, received: {}, calculated: {}",
                                   *downloadRequest,
                                   strCacheKey,
                                   receivedChecksum,
                                   calculatedChecksum);

                    cacheClientStatus = CacheClientStatus::CHECKSUM_MISMATCH;
                    m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), false);
                    return cacheClientStatus;
                }
            }
            else
            {
                LOG_DEBUG_REQID("Checksum not found in metadata, key: {}", *downloadRequest, strCacheKey);
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

    m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), false);
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
    for(std::size_t i = 0; i < m_nNumServers; i++)
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

        std::unique_ptr<IConnection> connection = move(m_spConnectionManager->GetConnection(i));
        std::string& hostname = m_vecServers[i];

        if(!connection || !connection.get())
        {
            LOG_ERROR_REQID("Couldn't get server connection {}, delete request for groupID: {}",
                            *deleteGroupRequest,
                            hostname,
                            groupID);
            m_spConnectionManager->ReleaseConnection(i, move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        if(!SendMessageToRemote(connection.get(), request))
        {
            LOG_ERROR_REQID("Couldn't send message to server {}, delete request for groupID: {}",
                            *deleteGroupRequest,
                            hostname,
                            groupID);
            m_spConnectionManager->ReleaseConnection(i, move(connection), true);
            return CacheClientStatus::INTERNAL_ERROR;
        }

        std::string requestMessage;
        if(!ReadMessageFromRemote(connection.get(), requestMessage))
        {
            LOG_ERROR_REQID("Couldn't read message from server {}, delete request for groupID: {}",
                            *deleteGroupRequest,
                            hostname,
                            groupID);
            m_spConnectionManager->ReleaseConnection(i, move(connection), true);
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
            m_spConnectionManager->ReleaseConnection(i, move(connection), false);
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
bool CacheClientImpl::IsInitialized() const
{
    return m_bInitialized;
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t CacheClientImpl::CalculateChecksum(/*[in]*/ const void* pBuffer, /*[in]*/ const std::size_t nBufferLength)
{
    if(pBuffer == nullptr || nBufferLength == 0)
    {
        LOG_INFO("Invalid buffer or length passed to CalculateChecksum");
        return 0;
    }
    boost::crc_32_type crc;
    crc.process_bytes(pBuffer, nBufferLength);
    uint32_t crcChecksum = crc.checksum();
    return crcChecksum;
}
//---------------------------------------------------------------------------------------------------------------------