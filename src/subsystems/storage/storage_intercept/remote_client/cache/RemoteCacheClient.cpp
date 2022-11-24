///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "RemoteCacheClient.h"
#include "StorageInterceptLogger.h"
#include "cache.pb.h"
#include "CacheUtils.h"
#include "StorageDlsymWrapper.h"
#include "StorageInterceptConfig.h"
#include <boost/algorithm/string.hpp>
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace cacheutils;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
RemoteCacheClient::RemoteCacheClient()
{
    int serverPort = StorageInterceptConfig::GetInstance().GetCacheServerPort();
    bool enableRemoteCache = StorageInterceptConfig::GetInstance().GetCacheEnableRemote();
    int numUploadThreads = StorageInterceptConfig::GetInstance().GetCacheNumUploadThreadsPerProcess();

    uint64_t maxUploadBufferSizePerProcess =
        StorageInterceptConfig::GetInstance().GetCacheMaxUploadBufferSizePerProcessGB() * 1024ULL * 1024 * 1024;

    // max size of 0 means unlimited
    if (maxUploadBufferSizePerProcess == 0)
    {
        maxUploadBufferSizePerProcess = std::numeric_limits<uint64_t>::max();
    }

    vector<string> vsServerNodes;
    if (enableRemoteCache)
    {
        string cacheServerList = StorageInterceptConfig::GetInstance().GetCacheServerList();
        boost::trim(cacheServerList);

        // using '#' as separator as ',' does not work when variables are passed using e2etest tool.
        boost::split(vsServerNodes, cacheServerList, boost::is_any_of("#"));
        if (!cacheServerList.empty() && vsServerNodes.size() > 0)
        {
            m_numServerNodes = vsServerNodes.size();
        }
        else
        {
            // if server list is not configured, we derive it using node-* convention used for instances within a job
            m_numServerNodes = StorageInterceptConfig::GetInstance().GetCacheNumServers();
            vsServerNodes.clear();
            for (int index = 0; index < m_numServerNodes; index++)
            {
                vsServerNodes.push_back(string("node-") + to_string(index));
            }
        }
    }
    else
    {
        m_numServerNodes = 1;
    }

    m_spConnectionManager = make_unique<ConnectionManager>(serverPort, !enableRemoteCache, vsServerNodes);
    m_spUploadManager = make_unique<CacheUploadManager>(
        *(m_spConnectionManager.get()),
        numUploadThreads,
        maxUploadBufferSizePerProcess);

    LoadEncryptionConfig();

    LOG_INFO("Configured RemoteCacheClient. numMounts: {}, numServers: {}, uploadThreads: {},"
        " buffersize: {} bytes",
        m_mapEncryptionUtil.size(),
        m_numServerNodes,
        numUploadThreads,
        maxUploadBufferSizePerProcess);
}
//---------------------------------------------------------------------------------------------------------------------
RemoteCacheClient::~RemoteCacheClient()
{
}
//---------------------------------------------------------------------------------------------------------------------
ICacheClient::CacheStatus RemoteCacheClient::DownloadSync(
    const std::string& mountName,
    const std::string& cacheKey,
    uint64_t startOffset,
    uint64_t length,
    char* buffer,
    uint64_t& readLength)
{
    ICacheClient::CacheStatus cacheStatus = ICacheClient::INTERNAL_ERROR;
    EncryptionUtil& encryptionUtil = GetEncryptionUtilForMount(mountName);

    if (encryptionUtil.IsEncryptionEnabled())
    {
        if (startOffset != 0)
        {
            // TODO: This can be supported by decrypting a full remote chunk and then returning only the subset.
            //       This requires a buffer copy too. 
            //       The interception client currently does not read partial chunks and hence we don't need this.
            LOG_ERROR("Partial download not supported with encryption, key: {}", cacheKey);
            return ICacheClient::INTERNAL_ERROR;
        }
    }

    Request request;
    DownloadRequest* downloadRequest = request.mutable_downloadrequest();
    downloadRequest->set_filename(cacheKey);
    downloadRequest->set_offset(startOffset);
    downloadRequest->set_length(length);
    downloadRequest->set_enablelock(true);

    if (!AddAuthParams(request))
    {
        LOG_WARN("failed to create auth request, key: {}", cacheKey);
        return ICacheClient::AUTH_FAILED;
    }

    int serverIndex = GetServerIndex(cacheKey, m_numServerNodes);
    auto serverConnection = move(m_spConnectionManager->GetConnection(serverIndex));
    if (!serverConnection.get())
    {
        LOG_WARN("couldn't get server connection, key: {}", cacheKey);
        return cacheStatus;
    }

    if (!SendMessageToRemoteFinal(serverConnection.get(), request))
    {
        LOG_WARN("couldn't send message to server, key: {}", cacheKey);
        m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
        return cacheStatus;
    }

    string responseMessage;
    if (!ReadMessageFromRemote(serverConnection.get(), responseMessage))
    {
        LOG_WARN("couldn't read message from server, key: {}", cacheKey);
        m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
        return cacheStatus;
    }

    DownloadResponse response;
    if (!response.ParseFromString(responseMessage))
    {
        LOG_WARN("error: Failed to parse protobuf message, key: {}", cacheKey);
        m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
        return cacheStatus;
    }

    LOG_DEBUG("Download response from server: {}, key: {}, filesize: {}",
        response.result(), cacheKey, response.filesize());
    if (response.result() == DownloadResponse::SUCCESS)
    {
        readLength = response.filesize();
        if (length > 0 && readLength > length)
        {
            LOG_WARN("Received invalid length from server, key: {}, got: {}, expected: {}",
                cacheKey, readLength, length);
            m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
            return cacheStatus;
        }

        if (readLength > 0)
        {
            bool bRet = encryptionUtil.DoDecrypt(response, serverConnection.get(), buffer, readLength);
            if (!bRet)
            {
                LOG_WARN("Failed to complete read from server, key: {}", cacheKey);
                m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
                return cacheStatus;
            }
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
    else if (response.result() == DownloadResponse::AUTH_FAILED)
    {
        cacheStatus = ICacheClient::AUTH_FAILED;
    }
    else
    {
        LOG_WARN("Server returned internal failure for key: {}", cacheKey);
        cacheStatus = ICacheClient::INTERNAL_ERROR;
    }

    m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), false);
    return cacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
bool RemoteCacheClient::UploadAsync(
    const std::string& mountName,
    const std::string& cacheKey,
    std::shared_ptr<const FileReadData> fileData)
{
    int serverIndex = GetServerIndex(cacheKey, m_numServerNodes);
    return m_spUploadManager->Upload(
        mountName,
        serverIndex,
        cacheKey,
        fileData,
        GetEncryptionUtilForMount(mountName));
}
//---------------------------------------------------------------------------------------------------------------------
void RemoteCacheClient::LoadEncryptionConfig()
{
    m_mapEncryptionUtil.clear();

    if (StorageInterceptConfig::GetInstance().GetMultipleMountEnabled())
    {
        std::vector<std::string> mountNames = StorageInterceptConfig::GetInstance().GetMountNames();
        for (const std::string& mountName : mountNames)
        {
            m_mapEncryptionUtil.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(mountName),
                std::forward_as_tuple(
                    StorageInterceptConfig::GetInstance().GetCacheEncryptionEnabled(mountName),
                    StorageInterceptConfig::GetInstance().GetCacheEncryptionKey(mountName)));
        }
    }
    else
    {
        m_mapEncryptionUtil.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(""),
            std::forward_as_tuple(
                StorageInterceptConfig::GetInstance().GetCacheEncryptionEnabled(""),
                StorageInterceptConfig::GetInstance().GetCacheEncryptionKey("")));
    }

    // Log the loaded config
    for (auto const& mountEncryptionUtil : m_mapEncryptionUtil)
    {
        LOG_INFO("Mount {} has encryption enabled={}",
            mountEncryptionUtil.first,
            mountEncryptionUtil.second.IsEncryptionEnabled());
    }
}
//---------------------------------------------------------------------------------------------------------------------
EncryptionUtil& RemoteCacheClient::GetEncryptionUtilForMount(/*[in] */ const std::string& mountName)
{
    return m_mapEncryptionUtil.at(mountName);
}
//---------------------------------------------------------------------------------------------------------------------