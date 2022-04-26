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
std::string StorageInterceptConfig::GetStoragePath()
{
    std::string storagePath = StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameStoragePath);

    // Remove trailing '/' from the storage path
    while (storagePath.back() == '/') storagePath.pop_back();

    return storagePath;
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetRemoteStoreType()
{
    return StorageInterceptConfig::GetConfigStr(m_ConfigProps, StorageInterceptConstants::ConfigNameRemoteType);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetMockClientRoot()
{
    return StorageInterceptConfig::GetConfigStr(m_ConfigProps, StorageInterceptConstants::ConfigNameMockClientRoot);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetAdlsHostName()
{
    return StorageInterceptConfig::GetConfigStr(m_ConfigProps, StorageInterceptConstants::ConfigNameAdlsHostName);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetAdlsClientId()
{
    return StorageInterceptConfig::GetConfigStr(m_ConfigProps, StorageInterceptConstants::ConfigNameAdlsClientId);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetAdlsClientSecret()
{
    return StorageInterceptConfig::GetConfigStr(m_ConfigProps, StorageInterceptConstants::ConfigNameAdlsClientSecret);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetAdlsAuthTokenUrl()
{
    return StorageInterceptConfig::GetConfigStr(m_ConfigProps, StorageInterceptConstants::ConfigNameAdlsAuthTokenUrl);
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
bool StorageInterceptConfig::GetCacheEnable()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps, StorageInterceptConstants::ConfigNameCacheEnable, false);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetCacheServerPort()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheServerPort,
        20000);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetCacheEnableRemote()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheEnableRemote,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetCacheUseLocalFS()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheUseLocalFS,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetCacheLocalFSCachePath()
{
    return StorageInterceptConfig::GetConfigStr(m_ConfigProps, StorageInterceptConstants::ConfigNameLocalFSCachePath);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetCacheNumServers()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(m_ConfigProps,
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
    return StorageInterceptConfig::GetOptionalConfigUint64(m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheUploaderThreadsPerProcess,
        2);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetCacheMaxUploadBufferSizePerProcess()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheUploaderMaxBufferSizePerProcess,
        1073741824 /* 1GB */);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetCacheReuseServerConnections()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheReuseServerConnections,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetAttributesCacheEnable()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps, StorageInterceptConstants::ConfigNameAttributesCacheEnable, false);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetAttributesCacheMaxEntries()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps, StorageInterceptConstants::ConfigNameAttributesCacheMaxEntries, 1000000);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetAttributesCacheUseLocalServer()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps, StorageInterceptConstants::ConfigNameAttributesCacheUseLocalServer, true);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetCacheAuthEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps, StorageInterceptConstants::ConfigNameCacheAuthEnabled, false);
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
bool StorageInterceptConfig::GetCacheEncryptionEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps, StorageInterceptConstants::ConfigNameCacheEncryptionEnabled, false);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetCacheEncryptionKey()
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheEncryptionKey,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetStorageAccountName()
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameAzBlobStorageAccName);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetStorageAccountKey()
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameAzBlobStorageAccKey,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetStorageSASToken()
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameAzBlobSASToken,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetBlobAccessToken()
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameAzBlobAccessToken,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetBlobContainerName()
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameAzBlobContainerName);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetStorageEndpointUrl()
{
    return StorageInterceptConfig::GetConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameAzBlobStorageEndpointUrl);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetMaxBlobConnectionsPerClient()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameMaxConnsPerBlobClient,
        10);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetChunkDownloadParallelismEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps, StorageInterceptConstants::ConfigNameChunkDownloadParallelismEnabled, true);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetChunkDownloadMaxConcurrency()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameChunkDownloadMaxConcurrency,
        GetMaxBlobConnectionsPerClient());
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetEnableLargePrefetch()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps, StorageInterceptConstants::ConfigNameLargePrefetchEnabled, false);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetPrefetchWindowSizeInMB()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNamePrefetchSizeInMB,
        100);
}
//---------------------------------------------------------------------------------------------------------------------