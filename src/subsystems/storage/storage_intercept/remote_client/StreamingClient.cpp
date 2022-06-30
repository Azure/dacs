///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StreamingClient.h"
#include "RemoteClientProvider.h"
#include "CacheClientFactory.h"
#include "CacheUtils.h"
#include "AttributesCacheFactory.h"
#include "InMemoryAttributesCache.h"
#include "PrefetchCache.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptConstants.h"
#include <cstring>
#include <algorithm>
#include <pthread.h>
#include <future>
//---------------------------------------------------------------------------------------------------------------------
// Helper struct used for tracking async chunk reads
struct AsyncReadChunkInfo
{
    AsyncReadChunkInfo(size_t chunkOffset, size_t readSize)
    : ChunkOffset(chunkOffset)
    , ExpectedReadSize(readSize)
    {
    }

    const size_t ChunkOffset;
    const size_t ExpectedReadSize;
    std::future<ssize_t> AsyncResult;

    bool isReady() const
    {
        return AsyncResult.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
};
//---------------------------------------------------------------------------------------------------------------------
StreamingClient::StreamingClient()
    : m_spRemoteClient(std::move(RemoteClientProvider::GetRemoteClient()))
    , m_nChunkSize(StorageInterceptConfig::GetInstance().GetStreamingChunkSize())
    , m_bEnableReadChunkParallelism(StorageInterceptConfig::GetInstance().GetChunkDownloadParallelismEnabled())
    , m_nMaxReadChunkConcurrency(StorageInterceptConfig::GetInstance().GetChunkDownloadMaxConcurrency())
    , m_bEnableLargePrefetch(StorageInterceptConfig::GetInstance().GetEnableLargePrefetch())
    , m_nPrefetchWindowSize(StorageInterceptConfig::GetInstance().GetPrefetchWindowSizeInMB() * (1*1024*1024))
    , m_nReadChunksInProgress(0)
    , m_nReadChunksTotal(0)
    , m_nReadChunksPrefetchCache(0)
    , m_nReadChunksRemoteCache(0)
    , m_nReadChunksRemoteClient(0)
    , m_nGetPropertiesTotal(0)
    , m_nGetPropertiesCache(0)
    , m_nGetPropertiesRemoteClient(0)
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

    // Sanitize prefetch window size
    const size_t maxPrefetchWindowSize = (size_t)m_nChunkSize * (PrefetchCache::GetInstance().MAX_ENTRIES_PER_FILE / 2);
    if (m_nPrefetchWindowSize > maxPrefetchWindowSize)
    {
        LOG_INFO(
            "Reducing prefetch size to {} from {} specified in config so that 2 prefetch windows can fit in cache",
            maxPrefetchWindowSize,
            m_nPrefetchWindowSize);
        m_nPrefetchWindowSize = maxPrefetchWindowSize;
    }
    if (m_nPrefetchWindowSize % m_nChunkSize != 0)
    {
        size_t newPrefetchWindowSize = m_nPrefetchWindowSize - (m_nPrefetchWindowSize % m_nChunkSize);
        LOG_INFO(
            "Reducing prefetch size to {} from {} specified in config so that it is a multiple of chunkSize",
            newPrefetchWindowSize,
            m_nPrefetchWindowSize);
        m_nPrefetchWindowSize = newPrefetchWindowSize;
    }

    LOG_INFO("StreamingClient initialized. ChunkSize={}, chunk cache: {}, attribute cache: {}"
             " readChunkParallelism={}, maxReadChunkConcurrency={},"
             " enableLargePrefetch: {}, prefetchWindowSizeInBytes: {}",
        m_nChunkSize,
        (m_spCacheClient ? "enabled" : "disabled"),
        (m_spAttributesCache ? "enabled" : "disabled"),
        m_bEnableReadChunkParallelism,
        m_nMaxReadChunkConcurrency,
        m_bEnableLargePrefetch,
        m_nPrefetchWindowSize);
}
StreamingClient::~StreamingClient()
{
    LogStats(true);
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
    if (StorageInterceptConfig::GetInstance().GetAttributesCacheEnable() &&
        StorageInterceptConfig::GetInstance().GetAttributesCacheUseLocalServer())
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
    if (StorageInterceptConfig::GetInstance().GetAttributesCacheEnable() &&
        StorageInterceptConfig::GetInstance().GetAttributesCacheUseLocalServer())
    {
        const std::shared_ptr<IAttributesCache> emptyAttributesCache;
        std::atomic_store(&m_spAttributesCache, emptyAttributesCache);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::OpenFile(const std::string& remotePath)
{
    // No-op for now
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StreamingClient::ReadFile(
    const std::string& remotePath,
    void *buf,
    size_t readSize,
    off64_t readOffset)
{
    // If large prefetch is enabled and read size less than prefetch window size,
    // prefetch the window first and then read and return.
    //
    // Note: Large Prefetch optimization is only helpful in case of small sequential reads on a large file.
    if (m_bEnableLargePrefetch
        && readSize < m_nPrefetchWindowSize)
    {
        EnsurePrefetchCacheWindowPopulated(remotePath, readSize, readOffset);

        // All data for this read is now in prefetchCache,
        // reading chunks parallely won't speed-up as there won't be remote store calls.
        return ReadChunksSequential(remotePath, buf, readSize, readOffset);
    }

    // If parallel chunk downloads are enabled and readSize is larger than chunk size,
    // read required chunks in parallel.
    if (m_bEnableReadChunkParallelism && readSize > m_nChunkSize)
    {
        return ReadChunksParallel(remotePath, buf, readSize, readOffset);
    }

    return ReadChunksSequential(remotePath, buf, readSize, readOffset);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StreamingClient::ReadChunksSequential(
    const std::string& remotePath,
    void *buf,
    size_t readSize,
    off64_t readOffset)
{
    ssize_t nBytesRead = 0;
    const size_t readEnd = readOffset + readSize;

    // Divide into chunks and read sequentially
    off64_t chunkOffset = readOffset - (readOffset % m_nChunkSize);
    size_t bufOffset = 0;
    while(chunkOffset < readEnd)
    {
        // Proceed only when we are not exceeding the max allowed concurrency.
        PreReadChunkCheck();

        const off64_t currentOffset = readOffset + bufOffset;

        const size_t bufSizeForChunkCopy = std::min(readEnd, (size_t)m_nChunkSize + chunkOffset) - currentOffset;
        const size_t chunkCopyOffset = currentOffset - chunkOffset;

        ssize_t bytesReadIntoBuffer = ReadChunkAndCopyToBuffer(
            remotePath,
            chunkOffset,
            (char*)buf + bufOffset,
            bufSizeForChunkCopy,
            chunkCopyOffset);
        if (bytesReadIntoBuffer < 0)
        {
            LOG_ERROR("ReadChunkAndCopyToBuffer failed. remotePath={} chunkOffset={} currentOffset={}",
                remotePath,
                chunkOffset,
                currentOffset);
            throw "ReadChunkAndCopyToBuffer failed.";
        }

        bufOffset += bufSizeForChunkCopy;
        chunkOffset += m_nChunkSize;

        nBytesRead += bytesReadIntoBuffer;
        if (bytesReadIntoBuffer < bufSizeForChunkCopy)
        {
            // EOF has been reached
            break;
        }
    }

    return nBytesRead;
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StreamingClient::ReadChunksParallel(
    const std::string& remotePath,
    void *buf,
    size_t readSize,
    off64_t readOffset)
{
    const size_t readEnd = readOffset + readSize;

    // Divide into chunks and read parallely
    off64_t chunkOffset = readOffset - (readOffset % m_nChunkSize);
    size_t bufOffset = 0;
    std::vector<AsyncReadChunkInfo> asyncReadChunks;
    while(chunkOffset < readEnd)
    {
        // Proceed only when we are not exceeding the max allowed concurrency
        PreReadChunkCheck();

        const off64_t currentOffset = readOffset + bufOffset;

        const size_t bufSizeForChunkCopy = std::min(readEnd, (size_t)m_nChunkSize + chunkOffset) - currentOffset;
        const size_t chunkCopyOffset = currentOffset - chunkOffset;

        // start async read chunk and store future result.
        asyncReadChunks.push_back(AsyncReadChunkInfo(chunkOffset, bufSizeForChunkCopy));
        // TODO: use threadpool to avoid thread creation costs.
        asyncReadChunks.back().AsyncResult = std::async(
            std::launch::async,
            &StreamingClient::ReadChunkAndCopyToBuffer,
            this,
            std::ref(remotePath),
            chunkOffset,
            (char*)buf + bufOffset,
            bufSizeForChunkCopy,
            chunkCopyOffset);

        bufOffset += bufSizeForChunkCopy;
        chunkOffset += m_nChunkSize;
    }

    // Wait for all the async read chunks to complete
    LOG_DEBUG("Collecting the results of all read chunk calls. RemotePath={} ReadOffset={} ReadSize={} ReadChunks={}",
        remotePath,
        readOffset,
        readSize,
        asyncReadChunks.size());

    ssize_t nBytesRead = 0;
    for (AsyncReadChunkInfo& asyncReadChunk: asyncReadChunks)
    {
        ssize_t bytesRead = asyncReadChunk.AsyncResult.get();
        if (bytesRead < 0)
        {
            LOG_ERROR("ReadChunkAndCopyToBuffer failed. remotePath={} chunkOffset={}",
                remotePath,
                asyncReadChunk.ChunkOffset);
            throw "ReadChunkAndCopyToBuffer failed.";
        }
        else
        {
            LOG_DEBUG("ReadChunkAndCopyToBuffer completed. remotePath={} chunkOffset={}",
                remotePath,
                asyncReadChunk.ChunkOffset);
            nBytesRead += bytesRead;
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
    m_nGetPropertiesTotal++;

    // First check attributes cache
    IAttributesCache::CacheStatus cacheStatus;
    auto attributesCache = std::atomic_load(&m_spAttributesCache);
    if (attributesCache && 
        (cacheStatus = attributesCache->Get(remotePath, attributes)) == IAttributesCache::SUCCESS)
    {
        LOG_DEBUG("Found atrributes in cache for file: {}", remotePath);
        m_nGetPropertiesCache++;
        return 0;
    }

    int result = std::atomic_load(&m_spRemoteClient)->GetProperties(
        remotePath,
        attributes);
    m_nGetPropertiesRemoteClient++;
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
    return PrefetchCache::GetInstance().PurgeAll(remotePath);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StreamingClient::ReadChunkAndCopyToBuffer(
    const std::string& remotePath,
    off64_t chunkOffset,
    void* buf,
    size_t bufSize,
    size_t chunkCopyOffset)
{
    std::shared_ptr<const FileReadData> readChunkData = ReadChunk(remotePath, chunkOffset);
    if (!readChunkData)
    {
        LOG_ERROR("Read chunk failed. remotePath={} chunkOffset={}",
            remotePath,
            chunkOffset);
        throw "Read chunk failed.";
    }

    LOG_DEBUG("Read chunk done. remotePath={}, chunkOffset={}, chunkBytesRead={}",
        remotePath,
        chunkOffset,
        readChunkData->BufferSize);

    // Copy from the chunk read data into caller-provided buffer
    const size_t copySize = std::min(
        readChunkData->BufferSize - chunkCopyOffset,
        bufSize);
    if (copySize > 0)
    {
        memcpy(
            (char*)buf,
            readChunkData->Buffer.get() + chunkCopyOffset,
            copySize);
    }
    LOG_DEBUG("Copied into caller buffer. chunkOffset={} chunkCopyOffset={} copySize={}",
        chunkOffset,
        chunkCopyOffset,
        copySize);

    return copySize;
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<const FileReadData> StreamingClient::ReadChunk(
    const std::string& remotePath,
    off64_t chunkOffset)
{
    std::shared_ptr<const FileReadData> result = ReadChunkImpl(remotePath, chunkOffset);

    m_nReadChunksInProgress--;
    m_cvReadChunk.notify_one();

    return result;
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<const FileReadData> StreamingClient::ReadChunkImpl(
    const std::string& remotePath,
    off64_t chunkOffset)
{
    m_nReadChunksTotal++;
    LogStats();

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
        m_nReadChunksPrefetchCache++;
        return prefetchLookupResult;
    }

    std::shared_ptr<FileReadData> remoteReadData = std::make_shared<FileReadData>(chunkOffset, m_nChunkSize);

    // Try fetching from cache service
    std::string strCacheKey;
    ICacheClient::CacheStatus cacheStatus = ICacheClient::NOTFOUND;
    auto cacheClient = std::atomic_load(&m_spCacheClient);
    if (cacheClient)
    {
        strCacheKey = cacheutils::GetCacheKey(remotePath, chunkOffset);
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
            m_nReadChunksRemoteCache++;
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
        m_nReadChunksRemoteClient++;
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
void StreamingClient::EnsurePrefetchCacheWindowPopulated(
    const std::string& remotePath,
    size_t readSize,
    off64_t readOffset)
{
    off64_t startWindowOffset = readOffset - (readOffset % m_nPrefetchWindowSize);
    off64_t endOffset = readOffset + readSize;
    off64_t endWindowOffset = endOffset - (endOffset % m_nPrefetchWindowSize);

    const std::set<off64_t> cachedOffsets = PrefetchCache::GetInstance().GetCachedOffsets(remotePath);

    // check cached offsets to see how much of prefetch window(s) is already in cache
    off64_t prefetchStart = startWindowOffset;
    off64_t prefetchEnd = endWindowOffset + m_nPrefetchWindowSize;
    std::vector<off64_t> uncachedOffsets;
    for (off64_t chunkOffset = prefetchStart; chunkOffset < prefetchEnd; chunkOffset += m_nChunkSize)
    {
        if (std::find(cachedOffsets.begin(), cachedOffsets.end(), chunkOffset) == cachedOffsets.end())
        {
            uncachedOffsets.push_back(chunkOffset);
        }
    }

    if (uncachedOffsets.size() == 0)    return; // Prefetch window(s) already fully cached.

    // Purge some offsets if we are near the per-file limit of PrefetchCache.
    if (cachedOffsets.size() + uncachedOffsets.size() > PrefetchCache::GetInstance().MAX_ENTRIES_PER_FILE)
    {
        uint32_t nPurgeNeeded = PrefetchCache::GetInstance().MAX_ENTRIES_PER_FILE - (cachedOffsets.size() + uncachedOffsets.size());
        uint32_t nPurged = 0;

        // Pick the offsets to purge.
        // Note: We start purging from end, because we want the first few chunks to last to be considered for purging.
        //       This is because for a lot of use-cases, they contain metadata and are more likely to be re-read.
        std::vector<off64_t> offsetsToPurge;
        for (auto it = cachedOffsets.rbegin(); it != cachedOffsets.rend(); ++it)
        {
            off64_t cachedOffset = *it;
            if (cachedOffset < prefetchStart || cachedOffset >= prefetchEnd)
            {
                offsetsToPurge.push_back(cachedOffset);
                nPurged++;
            }

            if (nPurged >= nPurgeNeeded) break;
        }

        PrefetchCache::GetInstance().PurgeOffsets(remotePath, offsetsToPurge);
    }

    // Download the uncached chunks parallely in order to populate into pre-fetch cache
    std::vector<std::future<std::shared_ptr<const FileReadData>>> asyncReadChunks;

    LOG_INFO("Starting parallel async read chunk calls for populating PrefetchCache."
        " RemotePath={} PrefetchStart={} PrefetchEnd={} UncachedOffsets={} MaxConcurrency={}"
        " ReadStartOffset={} ReadEndOffset={}",
        remotePath,
        prefetchStart,
        prefetchEnd,
        uncachedOffsets.size(),
        m_nMaxReadChunkConcurrency,
        readOffset,
        endOffset);
    for (off64_t chunkOffset : uncachedOffsets)
    {
        // Proceed only when we are not exceeding the max allowed concurrency
        PreReadChunkCheck();

        // start async read chunk and store future result.
        asyncReadChunks.push_back(std::async(
            std::launch::async,
            &StreamingClient::ReadChunk,
            this,
            std::ref(remotePath),
            chunkOffset));
    }

    // Wait for all the async read chunks to complete
    LOG_DEBUG("Waiting for completion of all async read chunk calls for populating PrefetchCache window."
        " RemotePath={} PrefetchStart={} PrefetchEnd={} ReadChunks={}",
        remotePath,
        prefetchStart,
        prefetchEnd,        
        asyncReadChunks.size());
    ssize_t nBytesRead = 0;
    for (auto& asyncReadChunk: asyncReadChunks)
    {
        std::shared_ptr<const FileReadData> readChunkResult = asyncReadChunk.get();
        LOG_DEBUG("Completed async read chunk call for populating PrefetchCache window."
            " RemotePath={} ChunkOffset={}",
            remotePath,
            readChunkResult->Offset);
    }

    LOG_DEBUG("Completed all async read chunk calls for populating PrefetchCache."
        " RemotePath={} PrefetchStart={} PrefetchEnd={}",
        remotePath,
        prefetchStart,
        prefetchEnd);
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::PreReadChunkCheck()
{
    LOG_DEBUG("Enter PreReadChunkCheck. m_nReadChunksInProgress={}", m_nReadChunksInProgress);
    if (m_nReadChunksInProgress >= m_nMaxReadChunkConcurrency)
    {
        std::unique_lock<std::mutex> lock(m_mutexReadChunk);
        m_cvReadChunk.wait(lock,
        [this]
        {
            return this->m_nReadChunksInProgress < m_nMaxReadChunkConcurrency;
        });
    }

    m_nReadChunksInProgress++;
    LOG_DEBUG("Exit PreReadChunkCheck. m_nReadChunksInProgress={}", m_nReadChunksInProgress);
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::LogStats(bool forceLog)
{
    // TODO: To get these logs for low QPS cases, log at fixed inervals (every 2 minutes for e.g).
    if (forceLog || m_nReadChunksTotal % 200 == 0)
    {
        LOG_INFO("GetProperties stats: Total={} CacheHit={} CacheMiss={}",
            m_nGetPropertiesTotal,
            m_nGetPropertiesCache,
            m_nGetPropertiesRemoteClient);
        
        LOG_INFO("ReadChunk stats. Total={} PrefetchCache={} RemoteCache={} RemoteClient={}",
            m_nReadChunksTotal,
            m_nReadChunksPrefetchCache,
            m_nReadChunksRemoteCache,
            m_nReadChunksRemoteClient);
    }
}
//---------------------------------------------------------------------------------------------------------------------