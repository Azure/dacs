//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptConfig.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptLogger.h"
#include "StorageDlsymWrapper.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <sstream>
#include <boost/algorithm/string.hpp>
//---------------------------------------------------------------------------------------------------------------------
StorageInterceptConfig::StorageInterceptConfig()
    : m_IsConfigLoaded(false)
{
}
//---------------------------------------------------------------------------------------------------------------------
StorageInterceptConfig& StorageInterceptConfig::GetInstance()
{
    static StorageInterceptConfig s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
void StorageInterceptConfig::LoadConfig()
{
    // There should be no logging from this function to prevent deadlock with LFS interceptor.

    const mode_t CONFIG_FILE_OPEN_MODE=0777;
    const int XSTAT_VERSION=1;

    struct stat64 stBuf;
    if (StorageDlsymWrapper::xstat64(
        XSTAT_VERSION,
        StorageInterceptConstants::ConfigFilePath,
        &stBuf) != 0)
    {
        throw "Error getting stat for config file";
    }

    int nConfigFileHandle = StorageDlsymWrapper::open(
            StorageInterceptConstants::ConfigFilePath,
            O_RDONLY,
            CONFIG_FILE_OPEN_MODE);

    if (nConfigFileHandle < 0)
    {
        throw "Error opening config file";
    }

    size_t nConfigFileSize = stBuf.st_size;
    std::string strConfig;
    strConfig.resize(nConfigFileSize);
    ssize_t nConfigBytesRead = StorageDlsymWrapper::read(nConfigFileHandle, &strConfig[0], nConfigFileSize);

    /* Read the config file */
    while (nConfigBytesRead < nConfigFileSize)
    {
        ssize_t nConfigBytesReadNow =
            StorageDlsymWrapper::read(
                nConfigFileHandle,
                &strConfig[nConfigBytesRead],
                nConfigFileSize - nConfigBytesRead);
        if (nConfigBytesReadNow < 0)
        {
            throw "Error occurred while reading config file";
        }
        else if(nConfigBytesReadNow == 0)
        {
            /* Reached EOF */
            break;
        }
        nConfigBytesRead += nConfigBytesReadNow;
    }

    if(StorageDlsymWrapper::close(nConfigFileHandle) != 0)
    {
        throw "Error handling close for config file";
    }

    std::stringstream ssConfigStream;
    ssConfigStream.write(strConfig.c_str(), strConfig.size());
    ssConfigStream.seekg(0, std::ios_base::beg);
    std::string line;
    uint32_t lineNumber = 0;
    while (std::getline(ssConfigStream, line))
    {
        boost::trim(line);
        if (line.empty())
        {
            continue;
        }

        size_t idx = line.find(' ');
        if (idx == std::string::npos)
        {

            throw "Invalid config entry at lineNumber " + std::to_string(lineNumber);
        }

        std::string key = line.substr(0, idx);
        std::string value = line.substr(idx + 1);

        boost::trim(key);
        boost::trim(value);
        if (value.find("${") == 0 && value.back() == '}')
        {
            // Config entry expects an environment variable
            // key=${ENV}
            std::string strEnvKey = value.substr(2, value.length() - 3);
            char* envVal = std::getenv(strEnvKey.c_str());
            if (envVal == NULL)
            {
                throw "Missing environment variable=" + strEnvKey;
            }

            value = envVal;
        }
        m_ConfigProps[key] = value;

        lineNumber++;
    }

    if (lineNumber == 0)
    {
        throw "Config file is empty";
    }

    m_IsConfigLoaded = true;
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetConfigStr(
    const std::map<std::string, std::string>& config,
    const char* configName)
{
    if (!m_IsConfigLoaded)
    {
        LoadConfig();
    }

    auto it = config.find(configName);
    if (it != config.end())
    {
        return it->second;
    }

    LOG_ERROR("Required config with name={} not present", configName);
    throw ("Config not present. configName=" + std::string(configName));
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetConfigStr(
    const std::map<std::string, std::string>& config,
    const std::string& mountName,
    const char* configName)
{
    return StorageInterceptConfig::GetConfigStr(
        config,
        (mountName + configName).c_str());
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetOptionalConfigStr(
    const std::map<std::string, std::string>& config,
    const char* configName,
    const std::string& defaultValue)
{
    if (!m_IsConfigLoaded)
    {
        LoadConfig();
    }

    auto it = config.find(configName);
    if (it != config.end())
    {
        return it->second;
    }

    return defaultValue;
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetOptionalConfigStr(
    const std::map<std::string, std::string>& config,
    const std::string& mountName,
    const char* configName,
    const std::string& defaultValue)
{
    return GetOptionalConfigStr(
        config,
        (mountName + configName).c_str(),
        defaultValue);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptConfig::GetConfigInteger(
    const std::map<std::string, std::string>& config,
    const char* configName)
{
    auto strConfig = GetConfigStr(config, configName);
    return stoi(strConfig);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptConfig::GetOptionalConfigInteger(
    const std::map<std::string, std::string>& config,
    const char* configName,
    int defaultValue)
{
    auto strConfig = GetOptionalConfigStr(config, configName, "");
    if (strConfig.empty())
    {
        return defaultValue;
    }
    return stoi(strConfig);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetConfigUint64(
    const std::map<std::string, std::string>& config,
    const char* configName)
{
    auto strConfig = GetConfigStr(config, configName);
    return stoull(strConfig);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetOptionalConfigUint64(
    const std::map<std::string, std::string>& config,
    const char* configName,
    uint64_t defaultValue)
{
    auto strConfig = GetOptionalConfigStr(config, configName, "");
    if (strConfig.empty())
    {
        return defaultValue;
    }
    return stoull(strConfig);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetOptionalConfigUint64(
    const std::map<std::string, std::string>& config,
    const std::string& mountName,
    const char* configName,
    uint64_t defaultValue)
{
    return GetOptionalConfigUint64(
        config,
        (mountName + configName).c_str(),
        defaultValue);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::GetConfigUint32(
    const std::map<std::string, std::string>& config,
    const char* configName)
{
    auto strConfig = GetConfigStr(config, configName);
    return stoul(strConfig);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::GetOptionalConfigUint32(
    const std::map<std::string, std::string>& config,
    const char* configName,
    uint32_t defaultValue)
{
    auto strConfig = GetOptionalConfigStr(config, configName, "");
    if (strConfig.empty())
    {
        return defaultValue;
    }
    return stoul(strConfig);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetConfigBoolean(
    const std::map<std::string, std::string>& config,
    const char* configName)
{
    auto strConfig = GetConfigStr(config, configName);
    transform(strConfig.begin(), strConfig.end(), strConfig.begin(), ::tolower);
    return (strConfig == "true" || strConfig == "1");
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetOptionalConfigBoolean(
    const std::map<std::string, std::string>& config,
    const char* configName,
    bool defaultValue)
{
    auto strConfig = GetOptionalConfigStr(config, configName, "");
    if (strConfig.empty())
    {
        return defaultValue;
    }
    transform(strConfig.begin(), strConfig.end(), strConfig.begin(), ::tolower);
    return (strConfig == "true" || strConfig == "1");
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetOptionalConfigBoolean(
    const std::map<std::string, std::string>& config,
    const std::string& mountName,
    const char* configName,
    bool defaultValue)
{
    return GetOptionalConfigBoolean(
        config,
        (mountName + configName).c_str(),
        defaultValue);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetMultipleMountEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameMultiMountEnabled,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
std::vector<std::string> StorageInterceptConfig::GetMountNames()
{
    std::vector<std::string> mountNames;
    for (int mountIdx = 0; mountIdx < 10; mountIdx++)
    {
        std::string mountName = StorageInterceptConfig::GetOptionalConfigStr(
            m_ConfigProps,
            (StorageInterceptConstants::ConfigNamePrefixMountName + std::to_string(mountIdx)).c_str(),
            "");
        
        if (mountName != "")
        {
            mountNames.push_back(mountName);
        }
        else
        {
            break;
        }
    }

    return mountNames;
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetStoragePath(const std::string& mountName)
{
    std::string storagePath = StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameStoragePath);

    // Remove trailing '/' from the storage path
    while (storagePath.back() == '/') storagePath.pop_back();

    return storagePath;
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetRemoteStoreType(const std::string& mountName)
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameRemoteType);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetMockClientRoot(const std::string& mountName)
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameMockClientRoot);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetAdlsHostName(const std::string& mountName)
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAdlsHostName);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetAdlsClientId(const std::string& mountName)
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAdlsClientId);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetAdlsClientSecret(const std::string& mountName)
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAdlsClientSecret);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetAdlsAuthTokenUrl(const std::string& mountName)
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAdlsAuthTokenUrl);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::GetStreamingChunkSize()
{
    return StorageInterceptConfig::GetOptionalConfigUint32(
        m_ConfigProps, StorageInterceptConstants::ConfigNameStreamingChunkSize, 4 * 1024 * 1024);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::GetIoBlockSize()
{
    return StorageInterceptConfig::GetOptionalConfigUint32(
        m_ConfigProps, StorageInterceptConstants::ConfigNameIoBlockSize, 4 * 1024 * 1024);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetEnableReadIOLogs()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps, StorageInterceptConstants::ConfigNameEnableReadIOLogs, false);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetCacheEnable(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameCacheEnable,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetCacheServerPort()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheServerPort,
        20000);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetCacheEnableRemote()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheEnableRemote,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetCacheUseLocalFS()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheUseLocalFS,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetCacheLocalFSCachePath()
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameLocalFSCachePath);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetCacheLocalFSCacheStagingPath()
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameLocalFSCacheStagingPath);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetCacheLocalFSEnableIODirect()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameLocalFSCacheEnableIODirect,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetCacheLocalFSMemoryWarnThresholdGB()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(m_ConfigProps,
        StorageInterceptConstants::ConfigNameLocalFSCacheMemoryWarnThresholdGB,
        2);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetCacheNumServers()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheNumRemoteServers,
        1);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetCacheServerList()
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheServerList,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetCacheNumUploadThreadsPerProcess()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheUploaderThreadsPerProcess,
        4);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetCacheMaxUploadBufferSizePerProcessGB()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheUploaderMaxBufferSizePerProcessGB,
        10);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetCacheReuseServerConnections()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheReuseServerConnections,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetAttributesCacheEnable(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAttributesCacheEnable,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetAttributesCacheServerPort()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameAttributesCacheServerPort,
        20000);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetAttributesCacheMaxEntries()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameAttributesCacheMaxEntries,
        1000000);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetAttributesCacheUseLocalServer()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameAttributesCacheUseLocalServer,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetCacheAuthEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheAuthEnabled,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetCacheAuthAccountName()
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheAuthAccountName);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetCacheAuthAccountKey()
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheAuthAccountKey);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetCacheEncryptionEnabled(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameCacheEncryptionEnabled,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetCacheEncryptionKey(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameCacheEncryptionKey,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetCacheIBEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheIBEnabled,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetStorageAccountName(const std::string& mountName)
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobStorageAccName);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetStorageAccountKey(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobStorageAccKey,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetStorageSASToken(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobSASToken,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetBlobAccessToken(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobAccessToken,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetBlobContainerName(const std::string& mountName)
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobContainerName);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetStorageEndpointUrl(const std::string& mountName)
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobStorageEndpointUrl);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::IsDynamicContainerMount(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobDynamicContainer,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetMaxBlobConnectionsPerClient(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameMaxConnsPerBlobClient,
        10);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetChunkDownloadParallelismEnabled(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameChunkDownloadParallelismEnabled,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetChunkDownloadMaxConcurrency(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameChunkDownloadMaxConcurrency,
        GetMaxBlobConnectionsPerClient(mountName));
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetPrefetchCacheTotalSizeInMB()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigPrefetchCacheTotalSizeInMB,
        8000);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetPrefetchCachePerFileSizeInMB()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigPrefetchCachePerFileSizeInMB,
        800);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetEnableLargePrefetch(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameLargePrefetchEnabled,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetPrefetchWindowSizeInMB(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNamePrefetchSizeInMB,
        100);
}
//---------------------------------------------------------------------------------------------------------------------