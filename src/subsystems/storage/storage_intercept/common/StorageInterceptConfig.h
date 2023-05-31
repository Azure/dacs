//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// Utility to read config parameters.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include <map>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
class StorageInterceptConfig
{
    public:
        /* Singleton */
        static StorageInterceptConfig& GetInstance();

        // Getters for various config values.
        bool GetMultipleMountEnabled();

        std::vector<std::string> GetMountNames();

        std::string GetStoragePath(const std::string& mountName);

        std::string GetRemoteStoreType(const std::string& mountName);

        // Getters for ADLS config
        std::string GetAdlsHostName(const std::string& mountName);
        std::string GetAdlsClientId(const std::string& mountName);
        std::string GetAdlsClientSecret(const std::string& mountName);
        std::string GetAdlsAuthTokenUrl(const std::string& mountName);

        std::string GetMockClientRoot(const std::string& mountName);

        uint32_t GetStreamingChunkSize();

        uint32_t GetIoBlockSize();

        bool GetEnableReadIOLogs();

        // Getters for cache config
        bool GetCacheEnable(const std::string& mountName);
        uint64_t GetCacheServerPort();
        bool GetCacheEnableRemote();
        bool GetCacheUseLocalFS();
        std::string GetCacheLocalFSCachePath();
        std::string GetCacheLocalFSCacheStagingPath();
        bool GetCacheLocalFSEnableIODirect();
        uint64_t GetCacheLocalFSMemoryWarnThresholdGB();
        uint64_t GetCacheNumServers();
        std::string GetCacheServerList();
        uint64_t GetCacheNumUploadThreadsPerProcess();
        uint64_t GetCacheMaxUploadBufferSizePerProcessGB();
        bool GetCacheEnableSyncUpload();
        bool GetCacheReuseServerConnections();
        uint32_t GetCacheMaxConnectionsReuseSize();
        uint32_t GetCacheKeyExpirySeconds();
        std::string GetCacheKeyGroupId();

        // Getters for attribute cache
        bool GetAttributesCacheEnable(const std::string& mountName);
        uint64_t GetAttributesCacheServerPort();
        uint64_t GetAttributesCacheMaxEntries();
        bool GetAttributesCacheUseLocalServer();
        bool GetAttributesCacheUseRemoteServer();

        // Getters for authentication
        bool GetCacheAuthEnabled();
        std::string GetCacheAuthAccountName();
        std::string GetCacheAuthAccountKey();

        // Getters for encryption
        bool GetCacheEncryptionEnabled(const std::string& mountName);
        std::string GetCacheEncryptionKey(const std::string& mountName);

        // Getters for infiniband
        bool GetCacheIBEnabled();

        // Getters for blob config
        std::string GetStorageAccountName(const std::string& mountName);
        std::string GetStorageAccountKey(const std::string& mountName);
        std::string GetStorageSASToken(const std::string& mountName);
        std::string GetBlobAccessToken(const std::string& mountName);
        std::string GetStorageEndpointUrl(const std::string& mountName);
        std::string GetBlobContainerName(const std::string& mountName);
        bool IsDynamicContainerMount(const std::string& mountName);
        uint64_t GetMaxBlobConnectionsPerClient(const std::string& mountName);

        // Getters for chunk download parallelism
        bool GetChunkDownloadParallelismEnabled(const std::string& mountName);
        uint64_t GetChunkDownloadMaxConcurrency(const std::string& mountName);

        // Getters for prefetch cache
        uint64_t GetPrefetchCacheTotalSizeInMB();
        uint64_t GetPrefetchCachePerFileSizeInMB();

        // Getters for large prefetch        
        bool GetEnableLargePrefetch(const std::string& mountName);
        uint64_t GetPrefetchWindowSizeInMB(const std::string& mountName);

    private:
        // Constructor
        StorageInterceptConfig();

        // Load the config by reading from the config file
        void LoadConfig();

        // Overwrite config values with those defined in environment variables
        void LoadFromEnvVariables();

        // Private helper function to get config string value.
        std::string GetConfigStr(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const char* configName);
        
        // Private helper function to get config string value for a mount
        std::string GetConfigStr(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const std::string& mountName,
            /*[in]*/ const char* configName);

        // Private helper function to get config string value using default.
        std::string GetOptionalConfigStr(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const char* configName,
            /*[in]*/ const std::string& defaultValue);

        // Private helper function to get config string value using default for a mount
        std::string GetOptionalConfigStr(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const std::string& mountName,
            /*[in]*/ const char* configName,
            /*[in]*/ const std::string& defaultValue);

        // Private helper function to get config integer value.
        int GetConfigInteger(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const char* configName);

        // Private helper function to get config integer value using default.
        int GetOptionalConfigInteger(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const char* configName,
            /*[in]*/ int defaultValue);

        // Private helper function to get config uint64 value.
        uint64_t GetConfigUint64(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const char* configName);

        // Private helper function to get config uint64 value using default.
        uint64_t GetOptionalConfigUint64(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const char* configName,
            /*[in]*/ uint64_t defaultValue);

        // Private helper function to get config uint64 value using default.
        uint64_t GetOptionalConfigUint64(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const std::string& mountName,
            /*[in]*/ const char* configName,
            /*[in]*/ uint64_t defaultValue);

        // Private helper function to get config uint32 value.
        uint32_t GetConfigUint32(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const char* configName);

        // Private helper function to get config uint32 value using default.
        uint32_t GetOptionalConfigUint32(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const char* configName,
            /*[in]*/ uint32_t defaultValue);

        // Private helper function to get config boolean value.
        bool GetConfigBoolean(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const char* configName);

        // Private helper function to get config boolean value using default.
        bool GetOptionalConfigBoolean(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const char* configName,
            /*[in]*/ bool defaultValue);

        // Private helper function to get config boolean value using default.
        bool GetOptionalConfigBoolean(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const std::string& mountName,
            /*[in]*/ const char* configName,
            /*[in]*/ bool defaultValue);

        // Flag to see if config is loaded or not
        bool m_IsConfigLoaded;

        // Map containing config key values
        std::map<std::string, std::string> m_ConfigProps;
};
//---------------------------------------------------------------------------------------------------------------------