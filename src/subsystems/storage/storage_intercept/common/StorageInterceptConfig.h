//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// Utility to read config parameters.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include <map>
//---------------------------------------------------------------------------------------------------------------------
class StorageInterceptConfig
{
    public:
        /* Singleton */
        static StorageInterceptConfig& GetInstance();

        // Getters for various config values.
        std::string GetStoragePath();

        std::string GetRemoteStoreType();

        std::string GetAdlsHostName();

        std::string GetAdlsClientId();

        std::string GetAdlsClientSecret();

        std::string GetAdlsAuthTokenUrl();

        std::string GetMockClientRoot();

        uint32_t GetStreamingChunkSize();

        uint32_t GetIoBlockSize();

        // Getters for cache config
        bool GetCacheEnable();
        uint64_t GetCacheServerPort();
        bool GetCacheEnableRemote();
        bool GetCacheUseLocalFS();
        std::string GetCacheLocalFSCachePath();
        uint64_t GetCacheNumServers();
        std::string GetCacheServerList();
        uint64_t GetCacheNumUploadThreadsPerProcess();
        uint64_t GetCacheMaxUploadBufferSizePerProcess();
        uint64_t GetCacheReuseServerConnections();

        // Getters for attribute cache
        bool GetAttributesCacheEnable();
        uint64_t GetAttributesCacheMaxEntries();
        bool GetAttributesCacheUseLocalServer();

        // Getters for authentication
        bool GetCacheAuthEnabled();
        std::string GetCacheAuthAccountName();
        std::string GetCacheAuthAccountKey();

        // Getters for encryption
        bool GetCacheEncryptionEnabled();
        std::string GetCacheEncryptionKey();

        // Getters for blob config
        std::string GetStorageAccountName();
        std::string GetStorageAccountKey();
        std::string GetStorageSASToken();
        std::string GetStorageEndpointUrl();
        std::string GetBlobContainerName();
        uint64_t GetMaxBlobConnectionsPerClient();

        // Getters for chunk download parallelism
        bool GetChunkDownloadParallelismEnabled();
        uint64_t GetChunkDownloadMaxConcurrency();

        // Getters for large prefetch        
        bool GetEnableLargePrefetch();
        uint64_t GetPrefetchWindowSizeInMB();

    private:
        // Constructor
        StorageInterceptConfig();

        // Load the config by reading from the config file
        void LoadConfig();

        // Private helper function to get config string value.
        std::string GetConfigStr(
            /*[in]*/ const std::map<std::string, std::string>& config,
            /*[in]*/ const char* configName);

        // Private helper function to get config string value using default.
        std::string GetOptionalConfigStr(
            /*[in]*/ const std::map<std::string, std::string>& config,
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

        // Flag to see if config is loaded or not
        bool m_IsConfigLoaded;

        // Map containing config key values
        std::map<std::string, std::string> m_ConfigProps;
};
//---------------------------------------------------------------------------------------------------------------------