///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StreamingClient.h"
#include "RemoteClientProvider.h"
#include "CacheClientFactory.h"
#include "AttributesCacheFactory.h"
#include "InMemoryAttributesCache.h"
#include "PrefetchCache.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptLogger.h"
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <openssl/sha.h>
#include <pthread.h>
//---------------------------------------------------------------------------------------------------------------------
StreamingClient::StreamingClient()
    : m_spRemoteClient(std::move(RemoteClientProvider::GetRemoteClient())),
      m_nChunkSize(StorageInterceptConfig::GetInstance().GetStreamingChunkSize())
{
    // Register handlers to handle process fork
    if (pthread_atfork(StreamingClient::ForkHandlerPrepare,
        StreamingClient::ForkHandlerParent,
        StreamingClient::ForkHandlerChild) != 0)
    {
        LOG_WARN("Error setting pthread_atfork handlers");
    }

    if (StorageInterceptConfig::GetInstance().GetCacheEnable())
    {
        m_spCacheClient = std::move(CacheClientFactory::GetCacheClient());
    }
    if (StorageInterceptConfig::GetInstance().GetAttributesCacheEnable())
    {
        m_spAttributesCache = std::move(AttributesCacheFactory::GetAttributesCache());
    }
    LOG_INFO("StreamingClient initialized. ChunkSize={}, chunk cache: {}, attribute cache: {}",
        m_nChunkSize, (m_spCacheClient ? "enabled" : "disabled"), (m_spAttributesCache ? "enabled" : "disabled"));
}
//---------------------------------------------------------------------------------------------------------------------
StreamingClient& StreamingClient::GetInstance()
{
  static StreamingClient s_instance;
  return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::ForkHandlerPrepare()
{
    GetInstance().ClearClient();
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::ForkHandlerParent()
{
    GetInstance().SetClient();
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::ForkHandlerChild()
{
    GetInstance().SetClient();
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::SetClient()
{
    // Recreate the network clients so no connections gets duplicated across parent and child forked process.
    // TODO: We can let the parent use the original client and recreate clients only for the child forked process, 
    //       but there are a few corner cases which needs to be handled.
    LOG_INFO("Resetting clients after a process fork");
    std::atomic_store(&m_spRemoteClient, RemoteClientProvider::GetRemoteClient());
    if (StorageInterceptConfig::GetInstance().GetCacheEnable())
    {
        std::atomic_store(&m_spCacheClient, CacheClientFactory::GetCacheClient());
    }
    if (StorageInterceptConfig::GetInstance().GetAttributesCacheEnable())
    {
        std::atomic_store(&m_spAttributesCache, AttributesCacheFactory::GetAttributesCache());
    }
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::ClearClient()
{
    // Remove all stored connections and upload threads so they don't get duplicated after the fork.
    LOG_INFO("Clearing clients before a process fork");
    if (StorageInterceptConfig::GetInstance().GetCacheEnable())
    {
        const std::shared_ptr<ICacheClient> emptyCacheClient;
        std::atomic_store(&m_spCacheClient, emptyCacheClient);
    }
    if (StorageInterceptConfig::GetInstance().GetAttributesCacheEnable())
    {
        const std::shared_ptr<IAttributesCache> emptyAttributesCache;
        std::atomic_store(&m_spAttributesCache, emptyAttributesCache);
    }
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StreamingClient::ReadFile(
    const std::string& remotePath,
    void *buf,
    size_t readSize,
    off64_t readOffset)
{
    ssize_t nBytesRead = 0;
    const size_t readEnd = readOffset + readSize;

    // Keep reading sequentially in chunks till 'readSize' bytes have been read.
    while(nBytesRead < readSize)
    {
        const off64_t currentOffset = readOffset + nBytesRead;
        const off64_t chunkOffset = currentOffset - (currentOffset % m_nChunkSize);

        // Get the relevant chunk
        std::shared_ptr<const FileReadData> readChunkData = ReadChunk(
            remotePath,
            chunkOffset);
        if (!readChunkData)
        {
            LOG_ERROR("Read chunk failed. remotePath={} chunkOffset={} currentOffset={}",
                remotePath,
                chunkOffset,
                currentOffset);
            throw "Read chunk failed.";
        }

        LOG_DEBUG("Read chunk done. remotePath={}, chunkOffset={}, chunkBytesRead={}",
            remotePath,
            chunkOffset,
            readChunkData->BufferSize);

        // Copy from the chunk read data into caller-provided buffer
        const off64_t chunkCopyOffset = currentOffset - chunkOffset;
        const size_t copySize = std::min(
            readChunkData->BufferSize - chunkCopyOffset,
            readEnd - currentOffset);
        if (copySize > 0)
        {
            memcpy(
                (char*)buf + nBytesRead,
                readChunkData->Buffer.get() + chunkCopyOffset,
                copySize);
        }

        LOG_DEBUG("Copied into caller buffer. nBytesRead={} chunkCopyOffset={} copySize={}",
            nBytesRead,
            chunkCopyOffset,
            copySize);

        nBytesRead += copySize;
        if (readChunkData->BufferSize < m_nChunkSize)
        {
            // EOF has been reached.
            break;
        }
    }

    return nBytesRead;
}
//---------------------------------------------------------------------------------------------------------------------
std::string StreamingClient::ReadDirectory(
    const std::string& remotePath,
    const std::string& continuationToken,
    std::vector<DirectoryEntry>& dirEntries)
{
    const size_t numInitialEntries = dirEntries.size();
    const std::string strReturnVal = std::atomic_load(&m_spRemoteClient)->ReadDirectory(
        remotePath,
        continuationToken,
        dirEntries);
    
    // Update the attributes cache if new entries were found
    auto attributesCache = std::atomic_load(&m_spAttributesCache);
    if (attributesCache && dirEntries.size() > numInitialEntries)
    {
        attributesCache->PutAll(remotePath, dirEntries, numInitialEntries);
    }
    return strReturnVal;
}
//---------------------------------------------------------------------------------------------------------------------
int StreamingClient::GetProperties(
    const std::string& remotePath,
    FileAttributes& attributes)
{
    // First check attributes cache
    IAttributesCache::CacheStatus cacheStatus;
    auto attributesCache = std::atomic_load(&m_spAttributesCache);
    if (attributesCache && 
        (cacheStatus = attributesCache->Get(remotePath, attributes)) == IAttributesCache::SUCCESS)
    {
        LOG_DEBUG("Found atrributes in cache for file: {}", remotePath);
        return 0;
    }

    int result = std::atomic_load(&m_spRemoteClient)->GetProperties(
        remotePath,
        attributes);
    if (result >= 0 && attributesCache && cacheStatus == IAttributesCache::NOTFOUND_LOCKED)
    {
        // Update cache on success
        attributesCache->Put(remotePath, attributes);
    }

    return result;
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::Close(const std::string& remotePath)
{
    return PrefetchCache::GetInstance().Purge(remotePath);
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<const FileReadData> StreamingClient::ReadChunk(
    const std::string& remotePath,
    off64_t chunkOffset)
{
    if ((chunkOffset % m_nChunkSize) != 0)
    {
        LOG_ERROR("Invalid usage of readChunk. chunkOffset={} is not at chunk boundary.", chunkOffset);
        throw "Invalid usage of readChunk. ReadOffset is not at chunk boundary";
    }

    // Try fetch from prefetch cache
    std::shared_ptr<const FileReadData> prefetchLookupResult =
        PrefetchCache::GetInstance().Get(remotePath, chunkOffset);
    if (prefetchLookupResult)
    {
        LOG_DEBUG("Read file: {}, offset: {}, length: {} from prefetch cache",
            remotePath, chunkOffset, prefetchLookupResult->BufferSize);
        return prefetchLookupResult;
    }

    std::shared_ptr<FileReadData> remoteReadData = std::make_shared<FileReadData>(chunkOffset, m_nChunkSize);

    // Try fetching from cache service
    std::string strCacheKey;
    ICacheClient::CacheStatus cacheStatus = ICacheClient::NOTFOUND;
    auto cacheClient = std::atomic_load(&m_spCacheClient);
    if (cacheClient)
    {
        strCacheKey = GetCacheKey(remotePath, chunkOffset);
        LOG_DEBUG("Using cache key: {} for file: {}, offset: {}", strCacheKey, remotePath, chunkOffset);
        uint64_t readLength = 0;
        cacheStatus = cacheClient->DownloadSync(
            strCacheKey,
            0,
            m_nChunkSize,
            remoteReadData->Buffer.get(),
            readLength);
        if (cacheStatus == ICacheClient::SUCCESS)
        {
            LOG_DEBUG("Read file: {}, offset: {}, length: {} from cache", remotePath, chunkOffset, readLength);
            remoteReadData->BufferSize = readLength;
        }
        else if (cacheStatus == ICacheClient::INTERNAL_ERROR)
        {
            LOG_WARN("Got server internal error for cache key: {}, ignore and continue blob download", strCacheKey);
        }
    }

    if (cacheStatus != ICacheClient::SUCCESS)
    {
        // Fetch from remote storage   
        ssize_t nBytesFromRemote = std::atomic_load(&m_spRemoteClient)->ReadChunkSync(
            remotePath,
            remoteReadData->Buffer.get(),
            m_nChunkSize,
            chunkOffset);
        if (nBytesFromRemote == -1)
        {
            LOG_ERROR("Remote Client read failed. remotePath={} readSize={} readOffset={}",
                remotePath,
                m_nChunkSize,
                chunkOffset);
            return nullptr;
        }
        
        LOG_DEBUG("Read file: {}, offset: {}, length: {} from remote server",
            remotePath, chunkOffset, nBytesFromRemote);
        remoteReadData->BufferSize = nBytesFromRemote;
    }

    // upload to cache server if this client owns the cache key
    if (cacheClient && cacheStatus == ICacheClient::NOTFOUND_LOCKED)
    {
        bool bRet = cacheClient->UploadAsync(strCacheKey, remoteReadData);
        if (!bRet)
        {
            LOG_WARN("Failed to upload file: {}, offset: {} to cache", remotePath, chunkOffset);
            // TODO: can we retry this?
        }
    }
    
    // Put to pre-fetch cache
    PrefetchCache::GetInstance().Put(remotePath, remoteReadData);

    return remoteReadData;
}
//---------------------------------------------------------------------------------------------------------------------
std::string StreamingClient::GetCacheKey(
    const std::string& remotePath,
    off64_t chunkOffset)
{
    std::string strCacheKey = remotePath + ":" + std::to_string(chunkOffset);

    unsigned char shaHash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, strCacheKey.c_str(), strCacheKey.size());
    SHA256_Final(shaHash, &sha256);

    // hex encode the sha digest
    // TODO: this is not really necessary, review and remove
    std::stringstream ss;
    for(auto i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(shaHash[i]);
    }
    return ss.str();
}
//---------------------------------------------------------------------------------------------------------------------