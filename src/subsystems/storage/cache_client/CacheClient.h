// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CACHECLIENT_H_
#define CACHECLIENT_H_
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <stdbool.h>
#include <stdint.h>
//---------------------------------------------------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif
//---------------------------------------------------------------------------------------------------------------------
#define MAX_BUFFER_SIZE_BYTES (256 * 1024 * 1024)
#define MAX_CACHE_KEY_LEN 127
#define MAX_METADATA_KEY_VALUE_PAIRS 32
#define MAX_GROUPID_LEN 127
#define DEFAULT_SOCKET_BUF_SIZE 0
//---------------------------------------------------------------------------------------------------------------------
typedef enum
{
    SUCCESS,
    KEY_EXISTS,
    NOTFOUND,
    NOTFOUND_LOCKED,
    INVALID_ARGUMENT,
    // Deprecated: cache authentication has been removed. Retained (and never returned by the
    // cache server) so that the numeric values of subsequent entries do not shift.
    AUTH_FAILED,
    INTERNAL_ERROR,
    CHECKSUM_MISMATCH,
} CacheClientStatus;
//---------------------------------------------------------------------------------------------------------------------
typedef enum
{
    // Preserve compatibility with cache entries written before checksum metadata was enabled.
    // Missing checksum or algorithm metadata skips validation and the download succeeds.
    CHECKSUM_METADATA_OPTIONAL,
    // A full download cannot establish integrity without both checksum and algorithm metadata.
    // Missing or empty integrity metadata is reported as CHECKSUM_MISMATCH.
    CHECKSUM_METADATA_REQUIRED,
} ChecksumMetadataPolicy;
//---------------------------------------------------------------------------------------------------------------------
typedef struct
{
    char* strKey;
    char* strValue;
} KeyValuePair;
//---------------------------------------------------------------------------------------------------------------------
typedef struct
{
    KeyValuePair* arrKeyValuePairs;
    uint32_t nKeyValuePairs;
} Metadata;
//---------------------------------------------------------------------------------------------------------------------
// Deprecated: cache authentication has been removed. Retained only so that the Initialize()
// parameter list keeps its shape for existing source; Initialize() requires this to be NULL
// and rejects non-null values.
typedef struct
{
    char* m_strAccountName;
    char* m_strAccountKey;
} AuthCredentials;
//---------------------------------------------------------------------------------------------------------------------
typedef struct
{
    int64_t nKey;
    uint64_t nValue;
} DistKeyValuePair;
//---------------------------------------------------------------------------------------------------------------------
typedef struct
{
    DistKeyValuePair* arrKeyValuePairs;
    uint64_t nKeyValuePairs;
} LastAccessTimeDistribution;
//---------------------------------------------------------------------------------------------------------------------
typedef struct
{
    // Total number of attempts for retryable operations. Use 1 to disable retries (single attempt).
    uint32_t nMaxAttempts;
    // Base delay in milliseconds for exponential backoff.
    uint64_t nBaseDelayMillis;
    // Maximum delay in milliseconds between retries.
    uint64_t nMaxDelayMillis;
} CacheClientRetryConfig;
//---------------------------------------------------------------------------------------------------------------------
/**
 * Initialize cache client, setting up configuration parameters and optional built-in cache-server discovery.
 *
 * The checksum algorithm accepts only "xxhash32" and "crc32" (case-insensitive). A null, empty, or unsupported
 * value does not cause initialization to fail; the client silently defaults to "xxhash32". Applications that must
 * reject unsupported algorithms should validate this argument before calling Initialize().
 *
 * The cache-server ring can be supplied in either of two ways. A static list in `arrServers` fixes the ring for
 * the life of the process. Enabling discovery instead lets the client query a discovery service and refresh the
 * ring on a background thread, which is what callers whose cache-server set changes at runtime need.
 *
 * @param arrServers List of server hostnames like `cacheserver-0.cacheserver.default.svc.cluster.local`.
 *        When discovery is enabled this may be `NULL` with nServers 0. A `NULL` list with a non-zero nServers
 *        is always invalid.
 * @param nServers Size of the list `arrServers`
 * @param nPort Port number of the cache-servers and of the discovery service, must be between 1 and 65535
 * @param bEnableIB Enable InfiniBand when connecting to the cache-servers
 * @param bEnableCalculateChecksum Enable checksum calculation when uploading data to the cache-servers, (would impact performance)
 * @param bEnableValidateChecksum Enable checksum validation when downloading data from the cache-servers, (would impact performance)
 * @param strChecksumAlgorithm Algorithm to use for checksum calculation. default- xxhash32
 * @param pAuthCredentials Deprecated. Cache server authentication is no longer supported; this
 *        parameter is retained only for ABI compatibility. Must be `NULL` - passing a non-null
 *        value returns INVALID_ARGUMENT and the client is not initialized.
 * @param bEnableConsistentHashing Enable consistent hashing for server selection
 * @param retryConfig Retry configuration for transient cache failures. Retries are disabled if nMaxAttempts is 1.
 *        All values must be valid: nMaxAttempts >= 1, nBaseDelayMillis > 0, nMaxDelayMillis > 0, and nBaseDelayMillis <= nMaxDelayMillis.
 * @param bEnableCacheServerDiscovery Enable the built-in discovery client and background hash-ring refresh thread.
 * @param nCacheServerDiscoveryIntervalInSeconds Seconds between refreshes. Must be non-zero when discovery is
 *        enabled; ignored otherwise.
 * @param strCacheServerDiscoveryEndpoint Hostname or address of the discovery service. Required when discovery is
 *        enabled and ignored otherwise.
 * @param checksumMetadataPolicy Controls missing checksum metadata behavior for full downloads when
 *        bEnableValidateChecksum is true. CHECKSUM_METADATA_OPTIONAL preserves existing behavior by accepting
 *        entries without checksum or algorithm metadata. CHECKSUM_METADATA_REQUIRED returns CHECKSUM_MISMATCH
 *        when either field is missing or empty. Any other value defaults to CHECKSUM_METADATA_OPTIONAL.
 *        Partial downloads are not checksum validated.
 *
 * Initialization is one-shot: there is no Uninitialize(), and calling Initialize() again returns INTERNAL_ERROR
 * without changing any state. Re-calling it is not a way to update the server list.
 *
 * The three discovery parameters were appended to this signature rather than exposed through a separate entry
 * point, so callers built against an earlier header must be recompiled.
 *
 * Discovered server names are resolved with getaddrinfo() at connect time, so the process must run somewhere those
 * names resolve. In Kubernetes that means in-cluster using the cluster DNS resolver.
 *
 * @returns CacheClientStatus indicating the initialization status:
 * 1. SUCCESS - The cache client is initialized. With discovery enabled, this can be returned before the first
 *    successful discovery response; cache operations fail until a refresh populates the ring.
 * 2. INVALID_ARGUMENT - If the input parameters are invalid or out of range, e.g., nPort is larger than 65535,
 *    any of the server names in arrServers is null or empty, pAuthCredentials is non-null, or discovery is
 *    enabled without an endpoint or with a zero refresh interval.
 * 3. INTERNAL_ERROR - If the cache client is already initialized, or fails to initialize due to some internal error.
*/
CacheClientStatus Initialize(
    /*[inopt]*/ const char* const* arrServers,
    /*[in]   */ uint32_t nServers,
    /*[in]   */ uint32_t nPort,
    /*[in]   */ bool bEnableIB,
    /*[in]   */ bool bEnableCalculateChecksum,
    /*[in]   */ bool bEnableValidateChecksum,
    /*[in]   */ const char* strChecksumAlgorithm,
    /*[in]   */ const AuthCredentials* pAuthCredentials,
    /*[in]   */ bool bEnableConsistentHashing,
    /*[in]   */ CacheClientRetryConfig retryConfig,
    /*[in]   */ bool bEnableCacheServerDiscovery,
    /*[in]   */ uint32_t nCacheServerDiscoveryIntervalInSeconds,
    /*[inopt]*/ const char* strCacheServerDiscoveryEndpoint,
    /*[in]   */ ChecksumMetadataPolicy checksumMetadataPolicy);
//---------------------------------------------------------------------------------------------------------------------
/**
 * Uploads nLength bytes of data from pBuffer to the cache server and associates it with the key strCacheKey.
 * The buffer size can be at most 128MB. Consumer of this API needs to divide the data into 128MB chunks and call
 * this API for each chunk separately.
 * 
 * This method chooses the cache server to upload the data to based on the hash value of strCacheKey and the
 * number of cache server instances.
 * 
 * This method also allows two special attributes to be associated with strCacheKey.
 * 1. nExpirySeconds -- the cache key strCacheKey and its associated data will be deleted after nExpirySeconds
 *    seconds.
 * 2. strGroupId -- the cache key strCacheKey will be associated with the group strGroupId. This allows all
 *    cache keys associated with the same group to be deleted together (refer method DeleteGroup).
 * 
 * Additionally, a bunch of key-value pairs can be associated with strCacheKey. The number of keys is
 * currently limited to 32. We may increase this limit in the future.
 * 
 * The data available in pBuffer can be optionally encrypted using the supplied encryption key.
 * 
 * The library will encrypt the data before uploading it to the cache server and decrypt
 * the data after downloading it from the cache server.
 * 
 * @param strCacheKey Key to be used to store the data, max supported length is 127 characters
 * @param pBuffer Pointer to the data to be uploaded
 * @param nLength Size of the data in bytes, which can be at most MAX_BUFFER_SIZE_BYTES (256MiB)
 * @param nExpirySeconds Number of seconds after which strCacheKey and its associated data shall be cleaned up from the
 *        cache server. Set the value to `0` to indicate that the key shall never expire
 * @param strGroupId Group ID of strCacheKey. Set the value to `NULL` to indicate that the data does not belong to any
 *        group
 * @param pMetadata Key-Value pairs to be associated with the data. Set the value to `NULL` to indicate empty metadata
 * @param strEncryptionKey C-string containing the encryption key used to encrypt the data. Set the value to `NULL` to
 *        indicate no encryption
 * 
 * @returns CacheClientStatus indicating the status of the upload operation
 * 1. SUCCESS -- The data and metadata was successfully uploaded to the cache server
 * 2. KEY_EXISTS -- The key strCacheKey already exists in the cache server. The data and metadata was not uploaded to
 *    the cache server
 * 3. INVALID_ARGUMENT -- If the input parameters are invalid or out of range, e.g., nLength > MAX_BUFFER_SIZE_BYTES (256MiB)
 * 4. INTERNAL_ERROR -- The data and metadata could not be uploaded to the cache server due to an internal error
*/
CacheClientStatus Upload(
    /*[in]*/ const char* strCacheKey,
    /*[in]*/ const void* pBuffer,
    /*[in]*/ const uint64_t nLength,
    /*[in]*/ const uint32_t nExpirySeconds,
    /*[in]*/ const char* strGroupId,
    /*[in]*/ const Metadata* pMetadata,
    /*[in]*/ const char* strEncryptionKey);
//---------------------------------------------------------------------------------------------------------------------
/**
 * Downloads nLength bytes of data associated with cache key strCacheKey from the cache server and stores it in
 * pBuffer. It chooses the cache server to download the data from based on the hash value of strCacheKey and the number
 * of cache server instances. The data available in pBuffer can be optionally decrypted using the encryption key
 * supplied. The library will decrypt the data before returning it to the caller. In this case, an additional temporary
 *  buffer of size nLength is created by the library for holding encrypted data.
 * 
 * @param strCacheKey Key to be used to download the data, max supported length is 127 characters
 * @param pBuffer Pointer to the buffer of length at most nLength bytes where downloaded data is copied. This buffer
 *        should be pre-allocated by the caller
 * @param nLength Size of pBuffer in bytes, this variable will be updated with the actual number of bytes downloaded,
 *        if the download is successful
 * @param pMetadata pointer to the Metadata object where the downloaded metadata associated with strCacheKey is stored.
 *        The arrKeyValuePairs field of the Metadata object is allocated by the library and should be freed by the caller.
 *        If set to `NULL`, metadata is not returned.
 * @param strEncryptionKey C-string containing the encryption key used to decrypt the data. Set the value to `NULL` to
 *        indicate no decryption needs to be performed
 * 
 * @returns CacheClientStatus indicating the status of the download operation
 * 1. SUCCESS -- The data was successfully downloaded from the cache server
 * 2. NOTFOUND_LOCKED -- The key strCacheKey does not exist in the cache server but it is locked for the current client
 *    for at most 60 seconds. The client owns the responsibility of uploading data for strCacheKey within 60 seconds.
 *    Other competing clients need not upload data for strCacheKey during this period. The clients thus cooperatively
 *    ensure that only one client uploads data for strCacheKey.
 * 3. NOTFOUND -- The key strCacheKey does not exist in the cache server and the client does not own the lock for the
 *    key. The client can either fetch the data from original source or it can continue calling Download for
 *    strCacheKey periodically until it gets SUCCESS or NOTFOUND_LOCKED
 * 4. INVALID_ARGUMENT -- If the input parameters are invalid or out of range, e.g., strlen(strCacheKey) > 127
 * 5. INTERNAL_ERROR -- The data could not be downloaded from the cache server due to an internal error
 */
CacheClientStatus Download(
    /*[in]    */ const char* strCacheKey,
    /*[out]   */ void* pBuffer,
    /*[inout] */ uint64_t* nLength,
    /*[out]*/ Metadata* pMetadata,
    /*[in] */ const char* strEncryptionKey);
//---------------------------------------------------------------------------------------------------------------------
/**
 * Upload to a specific cache server, bypassing consistent hash routing.
 * Same semantics as Upload(), but targets the specified server hostname.
 */
CacheClientStatus UploadToServer(
    /*[in]*/ const char* strTargetServer,
    /*[in]*/ const char* strCacheKey,
    /*[in]*/ const void* pBuffer,
    /*[in]*/ const uint64_t nLength,
    /*[in]*/ const uint32_t nExpirySeconds,
    /*[in]*/ const char* strGroupId,
    /*[in]*/ const Metadata* pMetadata,
    /*[in]*/ const char* strEncryptionKey);
//---------------------------------------------------------------------------------------------------------------------
/**
 * Download from a specific cache server, bypassing consistent hash routing.
 * Same semantics as Download(), but targets the specified server hostname.
 */
CacheClientStatus DownloadFromServer(
    /*[in]    */ const char* strTargetServer,
    /*[in]    */ const char* strCacheKey,
    /*[out]   */ void* pBuffer,
    /*[inout] */ uint64_t* nLength,
    /*[out]*/ Metadata* pMetadata,
    /*[in] */ const char* strEncryptionKey);
//---------------------------------------------------------------------------------------------------------------------
/**
 * Download a byte range from a specific cache server.
 * Same semantics as DownloadFromServer(), but reads only [nOffset, nOffset+*nLength) bytes.
 * Set nOffset=0 and *nLength to the full size for a complete download.
 *
 * @param strTargetServer Target cache server hostname
 * @param strCacheKey     Cache key to download
 * @param pBuffer         Pre-allocated output buffer of at least *nLength bytes
 * @param nOffset         Byte offset into the cached value to start reading from
 * @param nLength         [inout] In: max bytes to read. Out: actual bytes downloaded
 * @param pMetadata       [out] Optional metadata (set NULL to skip). Caller must free.
 * @param strEncryptionKey Encryption key (NULL for no decryption). Not supported with nOffset>0.
 *
 * @returns CacheClientStatus
 */
CacheClientStatus DownloadRangeFromServer(
    /*[in]    */ const char* strTargetServer,
    /*[in]    */ const char* strCacheKey,
    /*[out]   */ void* pBuffer,
    /*[in]    */ uint64_t nOffset,
    /*[inout] */ uint64_t* nLength,
    /*[outopt]*/ Metadata* pMetadata,
    /*[inopt] */ const char* strEncryptionKey);
//---------------------------------------------------------------------------------------------------------------------
/**
 * Delete all keys belonging to the group strGroupId and their associated data and metadata from all cache servers.
 * Deletion happens asynchronously in the background on all cache servers.
 * 
 * @param strGroupId Group ID to be used to delete the data
 * 
 * @returns CacheClientStatus indicating the status of the delete operation
 * 1. SUCCESS -- The delete operation was successfully initiated
 * 2. INTERNAL_ERROR -- The delete operation could not be initiated due to an internal error
 */
CacheClientStatus DeleteGroup(/*[in]*/ const char* strGroupId);
//---------------------------------------------------------------------------------------------------------------------
/**
 * Get the last access time distribution of cache chunks in the cache server grouped by the given distribution bucket size.
 * The distribution bucket is the time interval in hours.
 * Last access time distribution provides the total size of cache chunks that were accessed in each distribution bucket.
 * 
 * @param nDistributionBucketSizeInHours Distribution bucket size in hours.
 * @param pLastAccessTimeDistribution Key-Value pairs of cache chunk size mapped to distribution bucket start timestamp.
 * 
 * @returns CacheClientStatus indicating the status of the get access time distribution operation
 * 1. SUCCESS -- The access time distribution was successfully retrieved
 * 2. INTERNAL_ERROR -- The access time distribution could not be retrieved due to an internal error
 * 3. INVALID_ARGUMENT -- If the input parameters are invalid or out of range, e.g., nDistributionBucketSizeInHours <= 0
 */
CacheClientStatus GetLastAccessTimeDistribution(
    /*[in]*/ uint64_t nDistributionBucketSizeInHours,
    /*[out]*/ LastAccessTimeDistribution* pLastAccessTimeDistribution);

//---------------------------------------------------------------------------------------------------------------------
 /* Delete all cache entries having accesstime <= datetime(strLastAccessTime) from all cache servers.
 * Deletion happens asynchronously in the background on all cache servers.
 * 
 * @param strLastAccessTime  last access time as string in the ISO 8601 datetime format
 * ending with Z (zero UTC offset). The format is as follows:
 * YYYY-MM-DDThh:mm:ss.msecZ  Example: "2024-04-02T20:00:00.000Z"
 * 
 * @returns CacheClientStatus indicating the status of the delete operation
 * 1. SUCCESS -- The delete operation was successfully initiated
 * 2. INTERNAL_ERROR -- The delete operation could not be initiated due to an internal error
 * 3. INVALID_ARGUMENT -- If the input parameters are invalid or null
 */
CacheClientStatus DeleteByLastAccessTime(/*[in]*/ const char* strLastAccessTime);
//---------------------------------------------------------------------------------------------------------------------
#ifdef __cplusplus
}
#endif
//---------------------------------------------------------------------------------------------------------------------
#endif
//---------------------------------------------------------------------------------------------------------------------