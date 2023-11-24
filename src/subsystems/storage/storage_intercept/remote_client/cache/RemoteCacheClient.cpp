///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "RemoteCacheClient.h"

#include <boost/algorithm/string.hpp>
#include <dirent.h>
#include <map>

#include "CacheUtils.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptUtils.h"
#include "StorageLibcApiWrapper.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace cacheutils;
using namespace std;
using namespace StorageInterceptConstants;
//---------------------------------------------------------------------------------------------------------------------
RemoteCacheClient::RemoteCacheClient(/*[in] */ std::size_t nMaxBufferSize)
{
    int serverPort = StorageInterceptConfig::GetInstance().GetCacheServerPort();
    bool enableRemoteCache = StorageInterceptConfig::GetInstance().GetCacheEnableRemote();
    int numUploadThreads = StorageInterceptConfig::GetInstance().GetCacheNumUploadThreadsPerProcess();
    bool bCacheAuthEnabled = StorageInterceptConfig::GetInstance().GetCacheAuthEnabled();
    const std::string strCacheAccountName =
        bCacheAuthEnabled ? StorageInterceptConfig::GetInstance().GetCacheAuthAccountName() : "";
    const std::string strCacheAccountKey =
        bCacheAuthEnabled ? StorageInterceptConfig::GetInstance().GetCacheAuthAccountKey() : "";
    int nMaxConnectionsReuseSize = StorageInterceptConfig::GetInstance().GetCacheMaxConnectionsReuseSize();
    bool bReuseServerConnections = StorageInterceptConfig::GetInstance().GetCacheReuseServerConnections();
    bool bUseInfiniband = StorageInterceptConfig::GetInstance().GetCacheIBEnabled();
    bool bEnableCalculateChecksum = StorageInterceptConfig::GetInstance().IsCacheCalculateChecksumEnabled();
    bool bEnableValidateChecksum = StorageInterceptConfig::GetInstance().IsCacheValidateChecksumEnabled();

    uint64_t maxUploadBufferSizePerProcess =
        StorageInterceptConfig::GetInstance().GetCacheMaxUploadBufferSizePerProcessGB() * 1024ULL * 1024 * 1024;

    // max size of 0 means unlimited
    if(maxUploadBufferSizePerProcess == 0)
    {
        maxUploadBufferSizePerProcess = std::numeric_limits<uint64_t>::max();
    }

    vector<string> vsServerNodes;
    if(enableRemoteCache)
    {
        string cacheServerList = StorageInterceptConfig::GetInstance().GetCacheServerList();
        boost::trim(cacheServerList);

        // using '#' as separator as ',' does not work when variables are passed using e2etest tool.
        boost::split(vsServerNodes, cacheServerList, boost::is_any_of("#"));
        if(!cacheServerList.empty() && vsServerNodes.size() > 0)
        {
            m_numServerNodes = vsServerNodes.size();
        }
        else
        {
            // if server list is not configured, we derive it using node-* convention used for instances within a job
            m_numServerNodes = StorageInterceptConfig::GetInstance().GetCacheNumServers();
            vsServerNodes.clear();
            for(int index = 0; index < m_numServerNodes; index++)
            {
                vsServerNodes.push_back(string("node-") + to_string(index));
            }
        }
    }
    else
    {
        m_numServerNodes = 1;
    }

    bool bAuthEnabled = StorageInterceptConfig::GetInstance().GetCacheAuthEnabled();
    std::string accountName = bAuthEnabled ? StorageInterceptConfig::GetInstance().GetCacheAuthAccountName() : "";
    std::string accountKey = bAuthEnabled ? StorageInterceptConfig::GetInstance().GetCacheAuthAccountKey() : "";

    m_spCacheClientImpl = make_shared<CacheClientImpl>();

    const char** arrServers = new const char*[vsServerNodes.size()];
    for(size_t index = 0; index < vsServerNodes.size(); index++)
    {
        arrServers[index] = vsServerNodes[index].c_str();
    }

    auto cacheStatus = m_spCacheClientImpl->Initialize(arrServers,
                                                       vsServerNodes.size(),
                                                       serverPort,
                                                       bUseInfiniband,
                                                       bEnableCalculateChecksum,
                                                       bEnableValidateChecksum,
                                                       nMaxConnectionsReuseSize,
                                                       bAuthEnabled,
                                                       accountName.c_str(),
                                                       accountKey.c_str(),
                                                       !enableRemoteCache,
                                                       bReuseServerConnections,
                                                       nMaxBufferSize);
    STORAGE_ASSERT(cacheStatus == CacheClientStatus::SUCCESS, "Failed to initialize CacheClientImpl");
    delete[] arrServers;

    m_spUploadManager =
        make_unique<CacheUploadManager>(m_spCacheClientImpl, numUploadThreads, maxUploadBufferSizePerProcess);

    LoadMultiMountConfig();

    LOG_INFO("Configured RemoteCacheClient. numMounts: {}, numServers: {}, uploadThreads: {},"
             " buffersize: {} bytes",
             m_mapEncryptionKey.size(),
             m_numServerNodes,
             numUploadThreads,
             maxUploadBufferSizePerProcess);
}
//---------------------------------------------------------------------------------------------------------------------
RemoteCacheClient::~RemoteCacheClient()
{
}
//---------------------------------------------------------------------------------------------------------------------
CacheStatus RemoteCacheClient::DownloadSync(const std::string& mountName,
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
CacheStatus RemoteCacheClient::DownloadWithMetadataSync(const std::string& mountName,
                                                        const std::string& cacheKey,
                                                        uint64_t startOffset,
                                                        uint64_t length,
                                                        char* buffer,
                                                        uint64_t& readLength,
                                                        ::FileAttributes* fileAttributes)
{
    string strReqID = GetCorrelationId();
    CacheStatus cacheStatus = CacheStatus::INTERNAL_ERROR;

    // Encryption isn't supported for attribute download, initialize encryptionUtil for other cases
    const bool isAttrDownload = (fileAttributes != nullptr && length == 0);
    string strEncryptionKey = isAttrDownload ? "" : GetEncryptionKeyForMount(mountName);

    Metadata entryMetadata;
    CacheClientStatus cacheClientStatus =
        m_spCacheClientImpl->Download(cacheKey.c_str(),
                                      buffer,
                                      &length,
                                      &entryMetadata,
                                      strEncryptionKey.empty() ? nullptr : strEncryptionKey.c_str(),
                                      startOffset,
                                      true /* enableLock */,
                                      strReqID.c_str());
    readLength = length;

    if(fileAttributes && cacheClientStatus == CacheClientStatus::SUCCESS)
    {
        map<string, string> fileMetadata;
        for(int index = 0; index < entryMetadata.nKeyValuePairs; index++)
        {
            fileMetadata.emplace(entryMetadata.arrKeyValuePairs[index].strKey,
                                 entryMetadata.arrKeyValuePairs[index].strValue);

            free((void*)entryMetadata.arrKeyValuePairs[index].strKey);
            free((void*)entryMetadata.arrKeyValuePairs[index].strValue);
        }

        free((void*)(entryMetadata.arrKeyValuePairs));
        entryMetadata.arrKeyValuePairs = nullptr;
        entryMetadata.nKeyValuePairs = 0;

        auto strDirFlag = fileMetadata.find(CACHE_META_FILE_ISDIR);
        if(strDirFlag == fileMetadata.end())
        {
            LOG_WARN_REQID_STR("File Attributes isDir not set for the cache key {}", strReqID, cacheKey);
            return cacheStatus;
        }

        fileAttributes->isDir = (strDirFlag->second == "true");

        auto strSize = fileMetadata.find(CACHE_META_FILE_SIZE);
        if(strSize == fileMetadata.end())
        {
            LOG_WARN_REQID_STR("File Attributes size not set for the cache key {}", strReqID, cacheKey);
            return cacheStatus;
        }

        fileAttributes->size = std::strtoull(strSize->second.c_str(), nullptr, 0);

        auto strATime = fileMetadata.find(CACHE_META_FILE_ATIME);
        if(strATime == fileMetadata.end())
        {
            LOG_WARN_REQID_STR("File Attributes atime not set for the cache key {}", strReqID, cacheKey);
            return cacheStatus;
        }

        fileAttributes->aTime.tv_sec = std::strtoull(strATime->second.c_str(), nullptr, 0);

        auto strMTime = fileMetadata.find(CACHE_META_FILE_MTIME);
        if(strMTime == fileMetadata.end())
        {
            LOG_WARN_REQID_STR("File Attributes mtime not set for the cache key {}", strReqID, cacheKey);
            return cacheStatus;
        }

        fileAttributes->cTime.tv_sec = fileAttributes->mTime.tv_sec =
            std::strtoull(strMTime->second.c_str(), nullptr, 0);
    }

    switch(cacheClientStatus)
    {
    case CacheClientStatus::SUCCESS:
        cacheStatus = CacheStatus::SUCCESS;
        break;

    case CacheClientStatus::INTERNAL_ERROR:
        cacheStatus = CacheStatus::INTERNAL_ERROR;
        break;

    case CacheClientStatus::NOTFOUND:
        cacheStatus = CacheStatus::NOTFOUND;
        break;

    case CacheClientStatus::NOTFOUND_LOCKED:
        cacheStatus = CacheStatus::NOTFOUND_LOCKED;
        break;

    case CacheClientStatus::AUTH_FAILED:
        cacheStatus = CacheStatus::AUTH_FAILED;
        break;

    case CacheClientStatus::CHECKSUM_MISMATCH:
        cacheStatus = CacheStatus::CHECKSUM_MISMATCH;
        break;

    default:
        cacheStatus = CacheStatus::INTERNAL_ERROR;
        break;
    }

    return cacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
bool RemoteCacheClient::Upload(const std::string& mountName,
                               const std::string& filePath,
                               off64_t fileOffset,
                               std::shared_ptr<const FileReadData> fileData)
{
    const std::string cacheKey = GetCacheKey(mountName, filePath, fileOffset);
    return m_spUploadManager->Upload(mountName, cacheKey, fileData, GetEncryptionKeyForMount(mountName), nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
CacheStatus RemoteCacheClient::Get(const std::string& path, ::FileAttributes& fileAttributes)
{
    // First check in memory cache
    if(m_inMemAttributeCache.Get(path, fileAttributes) == CacheStatus::SUCCESS)
    {
        return CacheStatus::SUCCESS;
    }

    const string cacheKey = GetAttributeCacheKey(path);
    uint64_t readLength = 0;
    CacheStatus cacheStatus = DownloadWithMetadataSync("", cacheKey, 0, 0, nullptr, readLength, &fileAttributes);
    if(cacheStatus == CacheStatus::SUCCESS)
    {
        m_inMemAttributeCache.Put(path, fileAttributes);
    }

    return cacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
void RemoteCacheClient::Put(const std::string& path, const ::FileAttributes& fileAttributes)
{
    m_inMemAttributeCache.Put(path, fileAttributes);
    const string cacheKey = GetAttributeCacheKey(path);
    const string strEcncryptionKey = "";
    m_spUploadManager->Upload("", cacheKey, nullptr, strEcncryptionKey, make_unique<::FileAttributes>(fileAttributes));
}
//---------------------------------------------------------------------------------------------------------------------
void RemoteCacheClient::PutAll(const std::string& pathPrefix,
                               const std::vector<DirectoryEntry>& dirEntries,
                               const size_t startIndex)
{
    int numEntries = dirEntries.size() - startIndex;
    if(numEntries <= 0)
    {
        return;
    }

    m_inMemAttributeCache.PutAll(pathPrefix, dirEntries, startIndex);

    LOG_DEBUG("Adding {} entries to remote attribute cache", numEntries);
    for(size_t index = startIndex; index < dirEntries.size(); index++)
    {
        const DirectoryEntry& dirEntry = dirEntries[index];
        ::FileAttributes fileAttributes;
        fileAttributes.isDir = (dirEntry.d_type == DT_DIR);
        fileAttributes.size = dirEntry.d_size;
        fileAttributes.mTime.tv_sec = dirEntry.d_mtime;
        fileAttributes.aTime.tv_sec = dirEntry.d_atime;
        fileAttributes.cTime = fileAttributes.mTime;

        const string cacheKey = GetAttributeCacheKey(StorageInterceptUtils::CombinePath(pathPrefix, dirEntry.d_name));
        const string strEcncryptionKey = "";
        m_spUploadManager->Upload(
            "", cacheKey, nullptr, strEcncryptionKey, make_unique<::FileAttributes>(fileAttributes));
    }
}
//---------------------------------------------------------------------------------------------------------------------
void RemoteCacheClient::LoadMultiMountConfig()
{
    m_mapEncryptionKey.clear();
    m_strCachePrefixMap.clear();

    if(StorageInterceptConfig::GetInstance().GetMultipleMountEnabled())
    {
        std::vector<std::string> mountNames = StorageInterceptConfig::GetInstance().GetMountNames();
        for(const std::string& mountName : mountNames)
        {
            bool enableEncryption = StorageInterceptConfig::GetInstance().GetCacheEncryptionEnabled(mountName);
            const std::string encKey =
                enableEncryption ? StorageInterceptConfig::GetInstance().GetCacheEncryptionKey(mountName) : "";

            m_mapEncryptionKey.try_emplace(mountName, encKey);

            // If encryption is enabled then the file contents will be different depending on the encryption
            // key used by the client, hence treating the same file if encryted using different keys as different.
            m_strCachePrefixMap.emplace(mountName, enableEncryption ? encKey : "");
        }
    }
    else
    {
        bool enableEncryption = StorageInterceptConfig::GetInstance().GetCacheEncryptionEnabled("");
        const std::string encKey = StorageInterceptConfig::GetInstance().GetCacheEncryptionKey("");

        m_mapEncryptionKey.try_emplace("", enableEncryption ? encKey : "");

        m_strCachePrefixMap.emplace("", enableEncryption ? encKey : "");
    }

    // Log the loaded config
    for(auto const& mountEncryptionUtil : m_mapEncryptionKey)
    {
        LOG_INFO("Mount {} has encryption enabled={}", mountEncryptionUtil.first, !mountEncryptionUtil.second.empty());
    }
}
//---------------------------------------------------------------------------------------------------------------------
std::string& RemoteCacheClient::GetEncryptionKeyForMount(const std::string& mountName)
{
    return m_mapEncryptionKey.at(mountName);
}
//---------------------------------------------------------------------------------------------------------------------
std::string
RemoteCacheClient::GetCacheKey(const std::string& mountName, const std::string& filePath, off64_t fileOffset)
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