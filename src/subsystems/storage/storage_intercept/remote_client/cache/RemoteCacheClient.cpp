///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "RemoteCacheClient.h"
#include "StorageInterceptLogger.h"
#include "cache.pb.h"
#include "CacheUtils.h"
#include "StorageLibcApiWrapper.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptUtils.h"
#include <dirent.h>
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

    LoadMultiMountConfig();

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
CacheStatus RemoteCacheClient::DownloadSync(
    const std::string& mountName,
    const std::string& filePath,
    off64_t fileOffset,
    uint64_t startOffset,
    uint64_t length,
    char* buffer,
    uint64_t& readLength)
{
    const std::string cacheKey = GetCacheKey(mountName, filePath, fileOffset);
    return DownloadWithMetadataSync(mountName, cacheKey, startOffset, length, buffer, readLength, nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
CacheStatus RemoteCacheClient::DownloadWithMetadataSync(
    const std::string& mountName,
    const std::string& cacheKey,
    uint64_t startOffset,
    uint64_t length,
    char* buffer,
    uint64_t& readLength,
    ::FileAttributes* fileAttributes)
{
    CacheStatus cacheStatus = CacheStatus::INTERNAL_ERROR;
    EncryptionUtil& encryptionUtil = GetEncryptionUtilForMount(mountName);

    if (encryptionUtil.IsEncryptionEnabled())
    {
        if (startOffset != 0)
        {
            // TODO: This can be supported by decrypting a full remote chunk and then returning only the subset.
            //       This requires a buffer copy too. 
            //       The interception client currently does not read partial chunks and hence we don't need this.
            LOG_ERROR("Partial download not supported with encryption, key: {}", cacheKey);
            return CacheStatus::INTERNAL_ERROR;
        }
    }

    Request request;
    DownloadRequest* downloadRequest = request.mutable_downloadrequest();
    downloadRequest->set_requestid(cacheutils::GetCorrelationId());
    downloadRequest->set_filename(cacheKey);
    downloadRequest->set_offset(startOffset);
    downloadRequest->set_length(length);
    downloadRequest->set_enablelock(true);
    LOG_INFO_REQID("Creating DownloadRequest filename: {}, offset: {}, length: {}, enablelock: true",
        *downloadRequest, cacheKey, startOffset, length);

    if (!AddAuthParams(request))
    {
        LOG_WARN_REQID("failed to create auth request, key: {}", *downloadRequest, cacheKey);
        return CacheStatus::AUTH_FAILED;
    }

    int serverIndex = GetServerIndex(cacheKey, m_numServerNodes);
    auto serverConnection = move(m_spConnectionManager->GetConnection(serverIndex));
    if (!serverConnection.get())
    {
        LOG_WARN_REQID("couldn't get server connection, key: {}", *downloadRequest, cacheKey);
        return cacheStatus;
    }

    if (!SendMessageToRemoteFinal(serverConnection.get(), request))
    {
        LOG_WARN_REQID("couldn't send message to server, key: {}", *downloadRequest, cacheKey);
        m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
        return cacheStatus;
    }

    string responseMessage;
    if (!ReadMessageFromRemote(serverConnection.get(), responseMessage))
    {
        LOG_WARN_REQID("couldn't read message from server, key: {}", *downloadRequest, cacheKey);
        m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
        return cacheStatus;
    }

    DownloadResponse response;
    if (!response.ParseFromString(responseMessage))
    {
        LOG_WARN_REQID("error: Failed to parse protobuf message, key: {}", *downloadRequest, cacheKey);
        m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
        return cacheStatus;
    }

    LOG_DEBUG_REQID("Download response from server: {}, key: {}, filesize: {}",
         *downloadRequest, response.result(), cacheKey, response.filesize());
    if (response.result() == DownloadResponse::SUCCESS)
    {
        readLength = response.filesize();
        if (length > 0 && readLength > length)
        {
            LOG_WARN_REQID("Received invalid length from server, key: {}, got: {}, expected: {}",
                *downloadRequest, cacheKey, readLength, length);
            m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
            return cacheStatus;
        }

        if (readLength > 0)
        {
            if (!buffer)
            {
                LOG_ERROR_REQID("readLength > 0 but no read buffer supplied, key: {}", *downloadRequest,  cacheKey);
                m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
                return cacheStatus;
            }

            bool bRet = encryptionUtil.DoDecrypt(response, serverConnection.get(), buffer, readLength);
            if (!bRet)
            {
                LOG_WARN_REQID("Failed to complete read from server, key: {}", *downloadRequest,  cacheKey);
                m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
                return cacheStatus;
            }
        }

        if (fileAttributes)
        {
            // fetch file metadata from response
            auto& fileMetadata = response.metadata();

            auto strDirFlag = fileMetadata.find(CACHE_META_FILE_ISDIR);
            if (strDirFlag == fileMetadata.end())
            {
                LOG_WARN_REQID("File Attributes isDir not set for the cache key", *downloadRequest);
                m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
                return cacheStatus;
            }

            fileAttributes->isDir = (strDirFlag->second == "true");

            auto strSize = fileMetadata.find(CACHE_META_FILE_SIZE);
            if (strSize == fileMetadata.end())
            {
                LOG_WARN_REQID("File Attributes size not set for the cache key", *downloadRequest);
                m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
                return cacheStatus;
            }

            fileAttributes->size = std::strtoull(strSize->second.c_str(), nullptr, 0);

            auto strATime = fileMetadata.find(CACHE_META_FILE_ATIME);
            if (strATime == fileMetadata.end())
            {
                LOG_WARN_REQID("File Attributes atime not set for the cache key", *downloadRequest);
                m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
                return cacheStatus;
            }

            fileAttributes->aTime.tv_sec = std::strtoull(strATime->second.c_str(), nullptr, 0);

            auto strMTime = fileMetadata.find(CACHE_META_FILE_MTIME);
            if (strMTime == fileMetadata.end())
            {
                LOG_WARN_REQID("File Attributes mtime not set for the cache key", *downloadRequest);
                m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), true);
                return cacheStatus;
            }

            fileAttributes->cTime.tv_sec = fileAttributes->mTime.tv_sec = std::strtoull(strMTime->second.c_str(), nullptr, 0);
        }

        cacheStatus = CacheStatus::SUCCESS;
    }
    else if (response.result() == DownloadResponse::NOT_FOUND_GOT_LOCK)
    {
        cacheStatus = CacheStatus::NOTFOUND_LOCKED;
    }
    else if (response.result() == DownloadResponse::NOT_FOUND_ALREADY_LOCKED
        || response.result() == DownloadResponse::NOT_FOUND)
    {
        cacheStatus = CacheStatus::NOTFOUND;
    }
    else if (response.result() == DownloadResponse::AUTH_FAILED)
    {
        cacheStatus = CacheStatus::AUTH_FAILED;
    }
    else
    {
        LOG_WARN_REQID("Server returned internal failure for key: {}", *downloadRequest, cacheKey);
        cacheStatus = CacheStatus::INTERNAL_ERROR;
    }

    m_spConnectionManager->ReleaseConnection(serverIndex, move(serverConnection), false);
    return cacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
bool RemoteCacheClient::Upload(
    const std::string& mountName,
    const std::string& filePath,
    off64_t fileOffset,
    std::shared_ptr<const FileReadData> fileData)
{
    const std::string cacheKey = GetCacheKey(mountName, filePath, fileOffset);
    int serverIndex = GetServerIndex(cacheKey, m_numServerNodes);
    return m_spUploadManager->Upload(
        mountName,
        serverIndex,
        cacheKey,
        fileData,
        &GetEncryptionUtilForMount(mountName),
        nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
CacheStatus RemoteCacheClient::Get(
    const std::string& path,
    ::FileAttributes& fileAttributes)
{
    // First check in memory cache
    if (m_inMemAttributeCache.Get(path, fileAttributes) == CacheStatus::SUCCESS)
    {
        return CacheStatus::SUCCESS;
    }

    const string cacheKey = GetAttributeCacheKey(path);
    uint64_t readLength = 0;
    CacheStatus cacheStatus = DownloadWithMetadataSync(
        "", cacheKey, 0, 0, nullptr, readLength, &fileAttributes);
    if (cacheStatus == CacheStatus::SUCCESS)
    {
        m_inMemAttributeCache.Put(path, fileAttributes);
    }

    return cacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
void RemoteCacheClient::Put(
    const std::string& path,
    const ::FileAttributes& fileAttributes)
{
    m_inMemAttributeCache.Put(path, fileAttributes);
    const string cacheKey = GetAttributeCacheKey(path);
    int serverIndex = GetServerIndex(cacheKey, m_numServerNodes);
    m_spUploadManager->Upload("", serverIndex, cacheKey, nullptr, nullptr,
        make_unique<::FileAttributes>(fileAttributes));
}
//---------------------------------------------------------------------------------------------------------------------
void RemoteCacheClient::PutAll(
    const std::string& pathPrefix,
    const std::vector<DirectoryEntry>& dirEntries,
    const size_t startIndex)
{
    int numEntries = dirEntries.size() - startIndex;
    if (numEntries <= 0)
    {
        return;
    }

    m_inMemAttributeCache.PutAll(pathPrefix, dirEntries, startIndex);

    LOG_DEBUG("Adding {} entries to remote attribute cache", numEntries);
    for (size_t index = startIndex; index < dirEntries.size(); index++)
    {
        const DirectoryEntry& dirEntry = dirEntries[index];
        ::FileAttributes fileAttributes;
        fileAttributes.isDir = (dirEntry.d_type == DT_DIR);
        fileAttributes.size = dirEntry.d_size;
        fileAttributes.mTime.tv_sec = dirEntry.d_mtime;
        fileAttributes.aTime.tv_sec = dirEntry.d_atime;
        fileAttributes.cTime = fileAttributes.mTime;

        const string cacheKey = GetAttributeCacheKey(StorageInterceptUtils::CombinePath(pathPrefix, dirEntry.d_name));
        int serverIndex = GetServerIndex(cacheKey, m_numServerNodes);
        m_spUploadManager->Upload("", serverIndex, cacheKey, nullptr, nullptr,
            make_unique<::FileAttributes>(fileAttributes));
    }
}
//---------------------------------------------------------------------------------------------------------------------
void RemoteCacheClient::LoadMultiMountConfig()
{
    m_mapEncryptionUtil.clear();
    m_strCachePrefixMap.clear();

    if (StorageInterceptConfig::GetInstance().GetMultipleMountEnabled())
    {
        std::vector<std::string> mountNames = StorageInterceptConfig::GetInstance().GetMountNames();
        for (const std::string& mountName : mountNames)
        {
            bool enableEncryption = StorageInterceptConfig::GetInstance().GetCacheEncryptionEnabled(mountName);
            const std::string encKey = StorageInterceptConfig::GetInstance().GetCacheEncryptionKey(mountName);

            m_mapEncryptionUtil.try_emplace(mountName, enableEncryption, encKey);

            // If encryption is enabled then the file contents will be different depending on the encryption
            // key used by the client, hence treating the same file if encryted using different keys as different.
            m_strCachePrefixMap.emplace(
                mountName, 
                enableEncryption ? encKey : "");
        }
    }
    else
    {
        bool enableEncryption = StorageInterceptConfig::GetInstance().GetCacheEncryptionEnabled("");
        const std::string encKey = StorageInterceptConfig::GetInstance().GetCacheEncryptionKey("");

        m_mapEncryptionUtil.try_emplace("", enableEncryption, encKey);

        m_strCachePrefixMap.emplace(
            "", 
            enableEncryption ? encKey : "");
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
EncryptionUtil& RemoteCacheClient::GetEncryptionUtilForMount(const std::string& mountName)
{
    return m_mapEncryptionUtil.at(mountName);
}
//---------------------------------------------------------------------------------------------------------------------
std::string RemoteCacheClient::GetCacheKey(
    const std::string& mountName, const std::string& filePath, off64_t fileOffset)
{
    // To uniquely identify a chunk within a cluster we need 
    // <account name>/<container name>/<filepath within the container>:<file offset>
    return GetSHA256Hex(m_strCachePrefixMap.at(mountName) + "/" + filePath + ":" + std::to_string(fileOffset));
}
//---------------------------------------------------------------------------------------------------------------------
std::string RemoteCacheClient::GetAttributeCacheKey(const std::string& filePath)
{
    return GetSHA256Hex(filePath);
}
//---------------------------------------------------------------------------------------------------------------------