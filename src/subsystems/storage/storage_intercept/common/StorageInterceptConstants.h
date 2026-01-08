//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
//---------------------------------------------------------------------------------------------------------------------
namespace StorageInterceptConstants
{
    const char* const CacheKeyAccountNameOverride = "$accountNameOverride$";
    const char* const DefaultConfigFilePath = "/tmp/storageIntercept.config";
    const char* const ConfigFilePathEnv = "STORAGE_INTERCEPT_CONFIG_FILEPATH";
    const char* const ConfigEnvPrefix = "SI_";

    const char* const ConfigNameStoragePath = "storagePath";

    const char* const ConfigNameMultiMountEnabled = "multiMountEnabled";
    const char* const ConfigNamePrefixMountName = "mountName";

    const char* const ConfigNameRemoteType = "type";
    const char* const ConfigValRemoteTypeBlob = "blob";
    const char* const ConfigValRemoteTypeAdls = "adls";
    const char* const ConfigValRemoteTypeMock = "mock";

    const char* const ConfigNameMockClientRoot = "mockFsRoot";

    const char* const ConfigNameAdlsHostName = "adlsHostname";
    const char* const ConfigNameAdlsClientId = "adlsClientId";
    const char* const ConfigNameAdlsClientSecret = "adlsClientSecret";
    const char* const ConfigNameAdlsAuthTokenUrl = "adlsAuthTokenUrl";

    const char* const ConfigNameStreamingChunkSize = "streamingChunkSize";
    const char* const ConfigNameIoBlockSize = "ioBlockSize";
    const char* const ConfigNameEnableReadIOLogs = "enableReadIOLogs";
    const char* const ConfigNameLogRefreshIntervalInSeconds = "logRefreshIntervalInSeconds";

    // Config constants for caching
    const char* const ConfigNameCacheEnable = "cacheEnable";
    const char* const ConfigNameCacheServerPort = "cacheServerPort";
    const char* const ConfigNameCacheEnableRemote = "cacheEnableRemote";
    const char* const ConfigNameCacheUseLocalFS = "cacheUseLocalFS";
    const char* const ConfigNameLocalFSCachePath = "cacheLocalFSCachePath";
    const char* const ConfigNameLocalFSCacheStagingPath = "cacheLocalFSCacheStagingPath";
    const char* const ConfigNameLocalFSCacheEnableIODirect = "cacheLocalFSEnableIODirect";
    const char* const ConfigNameLocalFSCacheMemoryWarnThresholdGB = "cacheLocalFSMemoryWarnThresholdGB";
    const char* const ConfigNameCacheNumRemoteServers = "cacheNumServers";
    const char* const ConfigNameCacheServerList = "cacheServerList";
    const char* const ConfigNameCacheUploaderThreadsPerProcess = "cacheNumUploadThreadsPerProcess";
    const char* const ConfigNameCacheUploaderMaxBufferSizePerProcessGB = "cacheMaxUploadBufferSizePerProcessGB";
    const char* const ConfigNameCacheEnableSyncUpload = "cacheEnableSyncUpload";
    const char* const ConfigNameCacheReuseServerConnections = "cacheReuseServerConnections";
    const char* const ConfigNameCacheMaxConnectionsReuseSize = "cacheMaxConnectionsReuseSize";
    const char* const ConfigNameCacheKeyExpirySeconds = "cacheKeyExpirySeconds";
    const char* const ConfigNameCacheKeyGroupId = "cacheKeyGroupId";
    const char* const ConfigNameCacheAuthEnabled = "cacheAuthEnabled";
    const char* const ConfigNameCacheAuthAccountName = "cacheAuthAccountName";
    const char* const ConfigNameCacheAuthAccountKey = "cacheAuthAccountKey";
    const char* const ConfigNameCacheEncryptionEnabled = "cacheEncryptionEnabled";
    const char* const ConfigNameCacheEncryptionKey = "cacheEncryptionKey";
    const char* const ConfigNameCacheIBEnabled = "cacheIBEnabled";
    const char* const ConfigNameCacheCalculateChecksumEnabled = "cacheCalculateChecksumEnabled";
    const char* const ConfigNameCacheValidateChecksumEnabled = "cacheValidateChecksumEnabled";
    const char* const ConfigNameCacheChecksumAlgorithm = "cacheChecksumAlgorithm";
    const char* const ConfigDefaultCacheChecksumAlgorithm = "xxhash32";
    const char* const ConfigNameCacheServerDiscoveryEnabled = "cacheServerDiscoveryEnabled";
    const char* const ConfigNameCacheServerDiscoveryIntervalInSeconds = "cacheServerDiscoveryIntervalInSeconds";
    const char* const ConfigNameCacheServerDiscoveryEndpoint = "cacheServerDiscoveryEndpoint";
    const char* const ConfigNameCacheEnableConsistentHashing = "cacheEnableConsistentHashing";
    const char* const ConfigNameCacheRetryMaxAttempts = "cacheRetryMaxAttempts";
    const char* const ConfigNameCacheRetryBaseDelayMillis = "cacheRetryBaseDelayMillis";
    const char* const ConfigNameCacheRetryMaxDelayMillis = "cacheRetryMaxDelayMillis";

    // Config for Attributes cache
    const char* const ConfigNameAttributesCacheEnable = "attributesCacheEnable";
    const char* const ConfigNameAttributesCacheServerPort = "attributesCacheServerPort";
    const char* const ConfigNameAttributesCacheMaxEntries = "attributesCacheMaxEntries";
    const char* const ConfigNameAttributesCacheUseLocalServer = "attributesCacheUseLocalServer";
    const char* const ConfigNameAttributesCacheUseRemoteServer = "attributesCacheUseRemoteServer";
    const char* const ConfigNameAttributesCachePutAllOnReaddir = "attributesCachePutAllOnReaddir";

    // Config constants for blob client
    const char* const ConfigNameAzBlobStorageAccName = "azBlobStorageAccountName";
    const char* const ConfigNameAzBlobSASToken = "azBlobSASToken";
    const char* const ConfigNameAzBlobAccessToken = "azBlobAccessToken";
    const char* const ConfigNameAzBlobMSIEndpoint = "azBlobMSIEndpoint";
    const char* const ConfigNameAzBlobMSISecret = "azBlobMSISecret";
    const char* const ConfigNameAzBlobIdentityClientId = "azBlobIdentityClientId";
    const char* const ConfigNameAzBlobStorageAccKey = "azBlobStorageAccountKey";
    const char* const ConfigNameAzBlobStorageEndpointUrl = "azBlobStorageEndpoint";
    const char* const ConfigNameAzBlobContainerName = "azBlobContainerName";
    const char* const ConfigNameAzBlobDynamicAccount = "azBlobDynamicAccount";
    const char* const ConfigNameAzBlobDynamicContainer = "azBlobDynamicContainer";
    const char* const ConfigNameMaxConnsPerBlobClient = "maxConnsPerBlobClient";
    const char* const ConfigNameAzIgnoreAccountNameInCacheKey = "azIgnoreAccountNameInCacheKey";

    // Config constants for chunk download parallelism
    const char* const ConfigNameChunkOperationParallelismEnabled = "chunkOperationParallelismEnabled";
    const char* const ConfigNameChunkOperationMaxConcurrency = "chunkOperationMaxConcurrency";

    // Config constants for prefetch cache
    const char* const ConfigPrefetchCacheTotalSizeInMB = "prefetchCacheTotalSizeInMB";
    const char* const ConfigPrefetchCachePerFileSizeInMB = "prefetchCachePerFileSizeInMB";

    // Config constants for large prefetch
    const char* const ConfigNameLargePrefetchEnabled = "enableLargePrefetch";
    const char* const ConfigNamePrefetchSizeInMB = "prefetchWindowSizeInMB";

    // Config for avoiding remote downloads when cache lock is not held
    const char* const ConfigNameRemoteDownloadOnlyWhenLockHeldEnabled = "remoteDownloadOnlyWhenLockHeldEnabled";
    const char* const ConfigNameRemoteDownloadOnlyWhenLockHeldPollingIntervalInSecs = "remoteDownloadOnlyWhenLockHeldPollingIntervalInSeconds";

    // Config constants for enabling zero copy.
    // Benefitial for paths like sequential large file reads, aligned to streamingChunkSize
    const char* const ConfigNameEnableZeroCopy = "enableZeroCopy";

    //config for dynamic token fetching
    const char* const ConfigNameTokenRefreshBufferInSeconds = "tokenRefreshBufferInSeconds";
    const char* const ConfigNameTokenExpiryBufferInSeconds = "tokenExpiryBufferInSeconds";

    // Config for SSL verification in token requests
    const char* const ConfigNameTokenRequestEnableSSLVerification = "tokenRequestEnableSSLVerification";

    // Constants for file metadata
    const char* const CACHE_META_FILE_ISDIR = "FILE_ISDIR";
    const char* const CACHE_META_FILE_SIZE = "FILE_SIZE";
    const char* const CACHE_META_FILE_ATIME = "FILE_ATIME";
    const char* const CACHE_META_FILE_MTIME = "FILE_MTIME";

    // Config constants for write support
    const char* const ConfigNameWriteEnable = "enableWrite";
    const char* const ConfigNameWriteEnableRemote = "enableWriteRemote";
    const char* const ConfigNameWriteEnableRemoteAsync = "enableWriteRemoteAsync";
    const char* const ConfigNameRemoteAsyncWriteFileConcurrency = "remoteAsyncWriteFileConcurrency";
};
//---------------------------------------------------------------------------------------------------------------------