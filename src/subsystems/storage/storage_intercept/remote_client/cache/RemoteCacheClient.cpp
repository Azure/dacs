///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "RemoteCacheClient.h"
#include "StorageInterceptLogger.h"
#include "cache.pb.h"
#include "CacheUtils.h"
#include "StorageDlsymWrapper.h"
#include "StorageInterceptConfig.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace cacheutils;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
RemoteCacheClient::RemoteCacheClient()
{
    int serverPort = StorageInterceptConfig::GetInstance().GetCacheServerPort();
    bool enableRemoteCache = StorageInterceptConfig::GetInstance().GetCacheEnableRemote();
    m_numServerNodes = StorageInterceptConfig::GetInstance().GetCacheNumServers();
    int numUploadThreads = StorageInterceptConfig::GetInstance().GetCacheNumUploadThreadsPerProcess();
    uint64_t maxUploadBufferSizePerProcess =
        StorageInterceptConfig::GetInstance().GetCacheMaxUploadBufferSizePerProcess();
    
    m_spConnectionManager = make_unique<ConnectionManager>(serverPort, !enableRemoteCache, m_numServerNodes);
    m_spUploadManager = make_unique<CacheUploadManager>(
        *(m_spConnectionManager.get()),
        numUploadThreads,
        maxUploadBufferSizePerProcess);

    LOG_INFO("Configured RemoteCacheClient. numServers: {}, uploadThreads: {}, buffersize: {} bytes",
        m_numServerNodes, numUploadThreads, maxUploadBufferSizePerProcess);
}
//---------------------------------------------------------------------------------------------------------------------
RemoteCacheClient::~RemoteCacheClient()
{
}
//---------------------------------------------------------------------------------------------------------------------
ICacheClient::CacheStatus RemoteCacheClient::DownloadSync(
    const std::string& cacheKey,
    uint64_t startOffset,
    uint64_t length,
    char *buffer,
    uint64_t& readLength)
{
    ICacheClient::CacheStatus cacheStatus = ICacheClient::INTERNAL_ERROR;

    Request request;
    DownloadRequest* downloadRequest = request.mutable_downloadrequest();
    downloadRequest->set_filename(cacheKey);
    downloadRequest->set_offset(startOffset);
    downloadRequest->set_length(length);
    downloadRequest->set_enablelock(true);

    int serverIndex = GetServerIndex(cacheKey, m_numServerNodes);
    int serverFd = m_spConnectionManager->GetFd(serverIndex);
    if (serverFd < 0)
    {
        LOG_WARN("couldn't get server connection");
        return cacheStatus;
    }

    if (!SendMessageToRemote(serverFd, request))
    {
        LOG_WARN("couldn't send message to server");
        m_spConnectionManager->ReleaseFd(serverIndex, serverFd, true);
        return cacheStatus;
    }

    string requestMessage;
    if (!ReadMessageFromRemote(serverFd, requestMessage))
    {
        LOG_WARN("couldn't read message from server");
        m_spConnectionManager->ReleaseFd(serverIndex, serverFd, true);
        return cacheStatus;
    }

    DownloadResponse response;
    if (!response.ParseFromString(requestMessage))
    {
        LOG_WARN("error: Failed to parse protobuf message");
        m_spConnectionManager->ReleaseFd(serverIndex, serverFd, true);
        return cacheStatus;
    }

    LOG_DEBUG("Download response from server: {}, filesize: {}", response.result(), response.filesize());
    if (response.result() == DownloadResponse::SUCCESS)
    {
        readLength = response.filesize();
        if (length > 0 && readLength > length)
        {
            LOG_WARN("Received invalid length from server. got: {}, expected: {}", readLength, length);
            m_spConnectionManager->ReleaseFd(serverIndex, serverFd, true);
            return cacheStatus;
        }

        uint64_t totalRead = 0;
        while (totalRead < readLength)
        {
            int tempRead;
            if ((tempRead = TimedRead(serverFd, buffer + totalRead, readLength - totalRead)) > 0)
            {
                totalRead += tempRead;
            }
            else 
            {
                LOG_WARN("Failed to download data from server, fd={}", serverFd);
                m_spConnectionManager->ReleaseFd(serverIndex, serverFd, true);
                return cacheStatus;
            }
        }
        if (totalRead != readLength)
        {
            LOG_WARN("Failed to read correct length from server. got: {}, expected: {}", totalRead, readLength);
            m_spConnectionManager->ReleaseFd(serverIndex, serverFd, true);
            return cacheStatus;
        }
        cacheStatus = ICacheClient::SUCCESS;
    }
    else if (response.result() == DownloadResponse::NOT_FOUND_GOT_LOCK)
    {
        cacheStatus = ICacheClient::NOTFOUND_LOCKED;
    }
    else if (response.result() == DownloadResponse::NOT_FOUND_ALREADY_LOCKED
        || response.result() == DownloadResponse::NOT_FOUND)
    {
        cacheStatus = ICacheClient::NOTFOUND;
    }
    else
    {
        LOG_WARN("Server returned internal failure for key: {}", cacheKey);
        cacheStatus = ICacheClient::INTERNAL_ERROR;
    }

    m_spConnectionManager->ReleaseFd(serverIndex, serverFd, false);
    return cacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
bool RemoteCacheClient::UploadAsync(
    const std::string& cacheKey,
    std::shared_ptr<const FileReadData> fileData)
{
    int serverIndex = GetServerIndex(cacheKey, m_numServerNodes);
    return m_spUploadManager->Upload(serverIndex, cacheKey, fileData);
}
//---------------------------------------------------------------------------------------------------------------------
