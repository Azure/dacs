//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StorageInterceptConfig.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptLogger.h"
#include "StorageLibcApiWrapper.h"
#include "CacheClient.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
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
    // Use version 0 for __xstat64 system call to ensure cross-platform compatibility
    const int XSTAT_VERSION=0;

    std::string strConfigFilePath = StorageInterceptConstants::DefaultConfigFilePath;
    const char* configFilePath = std::getenv(StorageInterceptConstants::ConfigFilePathEnv);
    if (configFilePath != nullptr && configFilePath[0] != '\0')
    {
        strConfigFilePath = configFilePath;
    }

    struct stat64 stBuf;
    if (StorageLibcApiWrapper::xstat64(
        XSTAT_VERSION,
        strConfigFilePath.c_str(),
        &stBuf) != 0)
    {
        throw "Error getting stat for config file";
    }

    int nConfigFileHandle = StorageLibcApiWrapper::open(
            strConfigFilePath.c_str(),
            O_RDONLY,
            CONFIG_FILE_OPEN_MODE);

    if (nConfigFileHandle < 0)
    {
        throw "Error opening config file";
    }

    size_t nConfigFileSize = stBuf.st_size;
    std::string strConfig;
    strConfig.resize(nConfigFileSize);
    ssize_t nConfigBytesRead = StorageLibcApiWrapper::read(nConfigFileHandle, &strConfig[0], nConfigFileSize);

    /* Read the config file */
    while (nConfigBytesRead < nConfigFileSize)
    {
        ssize_t nConfigBytesReadNow =
            StorageLibcApiWrapper::read(
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

    if(StorageLibcApiWrapper::close(nConfigFileHandle) != 0)
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

        // Converting key to uppercase to make key search as case insensitive
        boost::to_upper(key);
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

    // Overwrite config values with those defined in environment variables
    LoadFromEnvVariables();

    if (lineNumber == 0)
    {
        throw "Config file is empty";
    }

    m_IsConfigLoaded = true;
}
//---------------------------------------------------------------------------------------------------------------------
void StorageInterceptConfig::LoadFromEnvVariables()
{
    extern char **environ;
    char **envVariableIter = environ;

    // Iterate over all environment variables set for this process
    while (*envVariableIter) {
        std::string envStr = *envVariableIter;
        boost::trim(envStr);

        // Check if the env matches storage interception env prefix
        if (envStr.find(StorageInterceptConstants::ConfigEnvPrefix) == 0)
        {
            // Discard the prefix and use the key
            envStr = envStr.substr(strlen(StorageInterceptConstants::ConfigEnvPrefix));
            size_t idxSeparater = envStr.find('=');
            if (idxSeparater != std::string::npos)
            {
                std::string envKey = envStr.substr(0, idxSeparater);
                std::string envValue = envStr.substr(idxSeparater + 1);

                boost::trim(envKey);
                boost::to_upper(envKey);
                boost::trim(envValue);

                // envValue can be empty, this will allow clearing of existing config value by using an environment variable.
                if (!envKey.empty())
                {
                    m_ConfigProps[envKey] = envValue;
                }
            }
        }

        envVariableIter++;
    }
}
//---------------------------------------------------------------------------------------------------------------------
#ifdef STORAGE_INTERCEPT_UT
void StorageInterceptConfig::OverrideConfig(const char* configName, const std::string& configValue)
{
    LOG_INFO("Overriding config. configName={} configValue={}", configName, configValue);
    m_ConfigProps[boost::to_upper_copy<std::string>(configName)] = configValue;
}
//---------------------------------------------------------------------------------------------------------------------
void StorageInterceptConfig::ReloadConfig()
{
    LOG_INFO("Reloading config");
    m_ConfigProps.clear();
    LoadConfig();
}
#endif
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetConfigStr(
    const std::map<std::string, std::string>& config,
    const char* configName)
{
    if (!m_IsConfigLoaded)
    {
        LoadConfig();
    }

    auto it = config.find(boost::to_upper_copy<std::string>(configName));
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

    auto it = config.find(boost::to_upper_copy<std::string>(configName));
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
uint32_t StorageInterceptConfig::GetLogsRefreshIntervalInSeconds()
{
    return StorageInterceptConfig::GetOptionalConfigUint32(
        m_ConfigProps, StorageInterceptConstants::ConfigNameLogRefreshIntervalInSeconds, 10);
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
bool StorageInterceptConfig::GetCacheEnableSyncUpload()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheEnableSyncUpload,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetCacheReuseServerConnections()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheReuseServerConnections,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::GetCacheMaxConnectionsReuseSize()
{
    return StorageInterceptConfig::GetOptionalConfigUint32(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheMaxConnectionsReuseSize,
        1024);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::GetCacheKeyExpirySeconds()
{
    return StorageInterceptConfig::GetOptionalConfigUint32(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheKeyExpirySeconds,
        0);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetCacheKeyGroupId()
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheKeyGroupId,
        "");
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
        GetCacheServerPort());
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetAttributesCacheMaxEntries()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameAttributesCacheMaxEntries,
        10000000);
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
bool StorageInterceptConfig::GetAttributesCacheUseRemoteServer()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps, StorageInterceptConstants::ConfigNameAttributesCacheUseRemoteServer, false);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetAttributesCachePutAllOnReaddir()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameAttributesCachePutAllOnReaddir,
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
bool StorageInterceptConfig::IsCacheCalculateChecksumEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheCalculateChecksumEnabled,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::IsCacheValidateChecksumEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheValidateChecksumEnabled,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetCacheChecksumAlgorithm()
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheChecksumAlgorithm,
        StorageInterceptConstants::ConfigDefaultCacheChecksumAlgorithm);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::GetCacheRetryMaxAttempts()
{
    return StorageInterceptConfig::GetOptionalConfigUint32(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheRetryMaxAttempts,
        1);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetCacheRetryBaseDelayMillis()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheRetryBaseDelayMillis,
        100);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetCacheRetryMaxDelayMillis()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheRetryMaxDelayMillis,
        5000);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptConfig::GetCacheSocketSndbufSize()
{
    return StorageInterceptConfig::GetOptionalConfigInteger(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheSocketSndbufSize,
        DEFAULT_SOCKET_BUF_SIZE);
}
//---------------------------------------------------------------------------------------------------------------------
int StorageInterceptConfig::GetCacheSocketRcvbufSize()
{
    return StorageInterceptConfig::GetOptionalConfigInteger(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheSocketRcvbufSize,
        DEFAULT_SOCKET_BUF_SIZE);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::IsRemoteDownloadOnlyWhenLockHeldEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameRemoteDownloadOnlyWhenLockHeldEnabled,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetRemoteDownloadOnlyWhenLockHeldPollingIntervalInSecs()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameRemoteDownloadOnlyWhenLockHeldPollingIntervalInSecs,
        2/* default */);
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
std::string StorageInterceptConfig::GetMSIEndpoint(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobMSIEndpoint,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetMSISecret(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobMSISecret,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetIdentityClientId(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobIdentityClientId,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetFederatedTokenFile(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobFederatedTokenFile,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetTenantId(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobTenantId,
        "");
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::GetAuthorityHost(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobAuthorityHost,
        "https://login.microsoftonline.com");
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetUseAzureIdentitySDK(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobUseAzureIdentitySDK,
        false);
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
bool StorageInterceptConfig::IsDynamicAccountMount(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzBlobDynamicAccount,
        false);
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
bool StorageInterceptConfig::IsIgnoreAccountNameInCacheKey(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameAzIgnoreAccountNameInCacheKey,
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
uint32_t StorageInterceptConfig::GetBlobRetryMaxAttempts()
{
    return StorageInterceptConfig::GetOptionalConfigUint32(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameBlobRetryMaxAttempts,
        StorageInterceptConstants::ConfigDefaultBlobRetryMaxAttempts);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::GetBlobRetryBaseDelaySeconds()
{
    return StorageInterceptConfig::GetOptionalConfigUint32(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameBlobRetryBaseDelaySeconds,
        StorageInterceptConstants::ConfigDefaultBlobRetryBaseDelaySeconds);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::GetBlobRetryMaxDelaySeconds()
{
    return StorageInterceptConfig::GetOptionalConfigUint32(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameBlobRetryMaxDelaySeconds,
        StorageInterceptConstants::ConfigDefaultBlobRetryMaxDelaySeconds);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetChunkOperationParallelismEnabled(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameChunkOperationParallelismEnabled,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
uint64_t StorageInterceptConfig::GetChunkOperationMaxConcurrency(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameChunkOperationMaxConcurrency,
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
        320);
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
        96);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::IsWriteSupportEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameWriteEnable,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::IsRemoteWriteEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameWriteEnableRemote,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::IsRemoteWriteAsyncEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameWriteEnableRemoteAsync,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::IsVersionedCacheKeysEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameEnableVersionedCacheKeys,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::GetRemoteAsyncWriteFileConcurrency()
{
    return StorageInterceptConfig::GetOptionalConfigUint32(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameRemoteAsyncWriteFileConcurrency,
        16);
} 
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetEnableZeroCopy(const std::string& mountName)
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        mountName,
        StorageInterceptConstants::ConfigNameEnableZeroCopy,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::GetTokenRefreshBufferInSeconds()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameTokenRefreshBufferInSeconds,
        DEFAULT_TOKEN_REFRESH_BUFFER_IN_SECONDS);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::GetTokenExpiryBufferInSeconds()
{
    return StorageInterceptConfig::GetOptionalConfigUint64(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameTokenExpiryBufferInSeconds,
        DEFAULT_TOKEN_EXPIRY_BUFFER_IN_SECONDS);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::GetEnableSSLVerification()
{
    return GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameTokenRequestEnableSSLVerification,
        true);
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::IsCacheServerDiscoveryEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheServerDiscoveryEnabled,
        false);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t StorageInterceptConfig::CacheServerDiscoveryIntervalInSeconds()
{
    return StorageInterceptConfig::GetOptionalConfigUint32(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheServerDiscoveryIntervalInSeconds,
        300);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StorageInterceptConfig::CacheServerDiscoveryEndpoint()
{
    return StorageInterceptConfig::GetOptionalConfigStr(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheServerDiscoveryEndpoint,
        "127.0.0.1");
}
//---------------------------------------------------------------------------------------------------------------------
bool StorageInterceptConfig::IsConsistentHashingEnabled()
{
    return StorageInterceptConfig::GetOptionalConfigBoolean(
        m_ConfigProps,
        StorageInterceptConstants::ConfigNameCacheEnableConsistentHashing,
        true);
}
//---------------------------------------------------------------------------------------------------------------------