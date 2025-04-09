///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "StreamingClient.h"
#include "RemoteClientProvider.h"
#include "CacheUtils.h"
#include "InMemoryAttributesCache.h"
#include "PrefetchCache.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptUtils.h"
#include "RemoteClientException.h"
#include <cstring>
#include <algorithm>
#include <numeric>
#include <pthread.h>
#include <future>
#include <thread>
//---------------------------------------------------------------------------------------------------------------------
using namespace cacheutils;
using namespace std;
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
StreamingClient::StreamingClient(
    const std::string& mountName,
    std::shared_ptr<ICacheClient> cacheClient,
    std::shared_ptr<IAttributesCache> attributesCache)
    : m_strMountName(mountName)
    , m_strRemotePathPrefix(StorageInterceptUtils::GetRemotePathPrefix(mountName))
    , m_enableReadIOLogs(StorageInterceptConfig::GetInstance().GetEnableReadIOLogs())
    , m_nChunkSize(StorageInterceptConfig::GetInstance().GetStreamingChunkSize())
    , m_bEnableReadChunkParallelism(
        StorageInterceptConfig::GetInstance().GetChunkDownloadParallelismEnabled(mountName))
    , m_nMaxReadChunkConcurrency(StorageInterceptConfig::GetInstance().GetChunkDownloadMaxConcurrency(mountName))
    , m_bEnableLargePrefetch(StorageInterceptConfig::GetInstance().GetEnableLargePrefetch(mountName))
    , m_nPrefetchWindowSize(StorageInterceptConfig::GetInstance().GetPrefetchWindowSizeInMB(mountName) * (1*1024*1024))
    , m_bRemoteDownloadOnlyWhenLockHeldEnabled(StorageInterceptConfig::GetInstance().IsRemoteDownloadOnlyWhenLockHeldEnabled())
    , m_nRemoteDownloadOnlyWhenLockHeldPollingIntervalInSecs(StorageInterceptConfig::GetInstance().GetRemoteDownloadOnlyWhenLockHeldPollingIntervalInSecs())
    , m_nRemoteDownloadOnlyWhenLockHeldMaxAttempts(50)
    , m_nReadChunksInProgress(0)
    , m_nReadChunksTotal(0)
    , m_nReadChunksPrefetchCache(0)
    , m_nReadChunksRemoteCache(0)
    , m_nReadChunksRemoteClient(0)
    , m_nGetPropertiesTotal(0)
    , m_nGetPropertiesCache(0)
    , m_nGetPropertiesRemoteClient(0)
    , m_vecCacheDownloadLatencies(m_nLatencySamplesVecSize, UINT_MAX)
    , m_vecRemoteDownloadLatencies(m_nLatencySamplesVecSize, UINT_MAX)
    , m_nTotalCacheDownloadLatencySamples(0)
    , m_nTotaRemotelDownloadLatencySamples(0)
    , m_nReadFileTotal({0, 0})
    , m_nReadTotalBytes({0, 0})
    , m_nRead128K({0, 0})
    , m_nRead4MB({0, 0})
    , m_nRead100MB({0, 0})
    , m_nRead1GB({0, 0})
    , m_nReadLarge({0, 0})
    , m_nReadMax({0, 0})
    , m_bStopLogging(false)
{
    SetClients(cacheClient, attributesCache);

    // Sanitize prefetch window size
    const size_t maxPrefetchWindowSize = (size_t)m_nChunkSize *
        (PrefetchCache::GetInstance().MAX_ENTRIES_PER_FILE / 2);
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

    // Initialize the logging thread here
    m_loggingThread = std::thread(
        &StreamingClient::LongRunningLogStats,
        this,
        StorageInterceptConfig::GetInstance().GetLogsRefreshIntervalInSeconds());

    LOG_INFO("StreamingClient initialized. MountName={}, RemotePathPrefix={} ChunkSize={}, chunk cache={},"
             " attribute cache={} readChunkParallelism={}, maxReadChunkConcurrency={},"
             " enableLargePrefetch={}, prefetchWindowSizeInBytes={}, Commit hash={} built on {}",
        mountName,
        m_strRemotePathPrefix,
        m_nChunkSize,
        (m_spCacheClient ? "enabled" : "disabled"),
        (m_spAttributesCache ? "enabled" : "disabled"),
        m_bEnableReadChunkParallelism,
        m_nMaxReadChunkConcurrency,
        m_bEnableLargePrefetch,
        m_nPrefetchWindowSize,
        BUILD_GIT_COMMIT_HASH,
        __DATE__);
}
//---------------------------------------------------------------------------------------------------------------------
StreamingClient::~StreamingClient()
{
    {
        std::lock_guard<std::mutex> lock(m_loggingMutex);
        m_bStopLogging = true;
    }
    m_loggingCV.notify_one();
    m_loggingThread.join();

    LogStats();
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::ForkHandlerPrepare()
{
    ClearClients();
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::ForkHandlerParent(
    std::shared_ptr<ICacheClient> cacheClient,
    std::shared_ptr<IAttributesCache> attributesCache)
{
    SetClients(cacheClient, attributesCache);
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::ForkHandlerChild(
    std::shared_ptr<ICacheClient> cacheClient,
    std::shared_ptr<IAttributesCache> attributesCache)
{
    SetClients(cacheClient, attributesCache);
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::SetClients(
    std::shared_ptr<ICacheClient> cacheClient,
    std::shared_ptr<IAttributesCache> attributesCache)
{
    // Recreate the network clients so no connections gets duplicated across parent and child forked process.
    // TODO: We can let the parent use the original client and recreate clients only for the child forked process, 
    //       but there are a few corner cases which needs to be handled.
    LOG_INFO("Resetting clients after a process fork");
    std::atomic_store(&m_spRemoteClient, RemoteClientProvider::GetRemoteClient(m_strMountName));
    if (StorageInterceptConfig::GetInstance().GetCacheEnable(m_strMountName))
    {
        std::atomic_store(&m_spCacheClient, cacheClient);
    }

    if (StorageInterceptConfig::GetInstance().GetAttributesCacheEnable(m_strMountName))
    {
        std::atomic_store(&m_spAttributesCache, attributesCache);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::ClearClients()
{
    // Remove all stored connections and upload threads so they don't get duplicated after the fork.
    LOG_INFO("Clearing clients before a process fork");
    if (StorageInterceptConfig::GetInstance().GetCacheEnable(m_strMountName))
    {
        const std::shared_ptr<ICacheClient> emptyCacheClient;
        std::atomic_store(&m_spCacheClient, emptyCacheClient);
    }
    
    if (StorageInterceptConfig::GetInstance().GetAttributesCacheEnable(m_strMountName))
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
    off64_t readOffset,
    off64_t fileSize)
{
    UpdateReadStats(readSize, false);

    if (readOffset >= fileSize)
    {
        return 0; // EOF reached.
    }

    if (readOffset + readSize > fileSize)
    {
        // Trim readSize so that we don't trigger unnecessary reads beyond file size.
        readSize = (fileSize - readOffset);
    }

    // If large prefetch is enabled and read size less than prefetch window size,
    // prefetch the window first and then read and return.
    //
    // Note: Large Prefetch optimization is only helpful in case of small sequential reads on a large file.
    if (m_bEnableLargePrefetch
        && readSize < m_nPrefetchWindowSize)
    {
        EnsurePrefetchCacheWindowPopulated(remotePath, readSize, readOffset, fileSize);

        // All data for this read is now in prefetchCache,
        // reading chunks parallely won't speed-up as there won't be remote store calls.
        ssize_t readBytes = ReadChunksSequential(remotePath, buf, readSize, readOffset);
        UpdateReadStats(readBytes, true);
        LogReadIO(remotePath, readOffset, readSize, readBytes);
        return readBytes;
    }

    // If parallel chunk downloads are enabled and readSize is larger than chunk size,
    // read required chunks in parallel.
    if (m_bEnableReadChunkParallelism && readSize > m_nChunkSize)
    {
        ssize_t readBytes = ReadChunksParallel(remotePath, buf, readSize, readOffset);
        UpdateReadStats(readBytes, true);
        LogReadIO(remotePath, readOffset, readSize, readBytes);
        return readBytes;
    }

    ssize_t readBytes = ReadChunksSequential(remotePath, buf, readSize, readOffset);
    UpdateReadStats(readBytes, true);
    LogReadIO(remotePath, readOffset, readSize, readBytes);
    return readBytes;
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
            throw RemoteClientException("InternalError: ReadChunkAndCopyToBuffer failed");
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
            throw RemoteClientException("InternalError: ReadChunkAndCopyToBuffer failed");
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
    std::string strReturnVal = std::atomic_load(&m_spRemoteClient)->ReadDirectory(
        remotePath,
        continuationToken,
        dirEntries);
    
    // Update the attributes cache if new entries were found
    auto attributesCache = std::atomic_load(&m_spAttributesCache);
    if (attributesCache && dirEntries.size() > numInitialEntries)
    {
        attributesCache->PutAll(
            GetCacheFilePath(remotePath),
            dirEntries,
            numInitialEntries);
    }
    return strReturnVal;
}
//---------------------------------------------------------------------------------------------------------------------
int StreamingClient::GetProperties(
    const std::string& remotePath,
    FileAttributes& attributes)
{
    m_nGetPropertiesTotal++;

    const std::string cacheFilePath = GetCacheFilePath(remotePath);

    // First check attributes cache
    CacheStatus cacheStatus;
    auto attributesCache = std::atomic_load(&m_spAttributesCache);
    if (attributesCache && 
        (cacheStatus = attributesCache->Get(cacheFilePath, attributes)) == CacheStatus::SUCCESS)
    {
        LOG_DEBUG("Found atrributes in cache for file: {}", remotePath);
        m_nGetPropertiesCache++;
        return 0;
    }

    int result = std::atomic_load(&m_spRemoteClient)->GetProperties(
        remotePath,
        attributes);

    m_nGetPropertiesRemoteClient++;
    if (result >= 0 && attributesCache && cacheStatus == CacheStatus::NOTFOUND_LOCKED)
    {
        // Update cache on success
        attributesCache->Put(cacheFilePath, attributes);
    }
    return result;
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::Close(const std::string& remotePath)
{
    return PrefetchCache::GetInstance().PurgeAll(GetCacheFilePath(remotePath));
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
        return -1;
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

    if ((chunkOffset % m_nChunkSize) != 0)
    {
        LOG_ERROR("Invalid usage of readChunk. chunkOffset={} is not at chunk boundary.", chunkOffset);
        return nullptr;
    }

    // Try fetch from prefetch cache
    const std::string cacheFilePath = GetCacheFilePath(remotePath);
    std::shared_ptr<const FileReadData> prefetchLookupResult =
        PrefetchCache::GetInstance().Get(cacheFilePath, chunkOffset);
    if (prefetchLookupResult)
    {
        LOG_DEBUG("Read file: {}, offset: {}, length: {} from prefetch cache",
            remotePath, chunkOffset, prefetchLookupResult->BufferSize);
        m_nReadChunksPrefetchCache++;
        return prefetchLookupResult;
    }

    std::shared_ptr<FileReadData> remoteReadData = std::make_shared<FileReadData>(chunkOffset, m_nChunkSize);

    // Try fetching from cache service
    CacheStatus cacheStatus = CacheStatus::NOTFOUND;
    auto cacheClient = std::atomic_load(&m_spCacheClient);

    if (cacheClient)
    {
        int nIterations = 0;
        do
        {
            if(nIterations > 0)
            {
                LOG_INFO("Failed to acquire entry/lock for cache key: {}, attempt: {}, retrying after {} seconds",
                    cacheFilePath,
                    nIterations,
                    m_nRemoteDownloadOnlyWhenLockHeldPollingIntervalInSecs);
                std::this_thread::sleep_for(std::chrono::seconds(m_nRemoteDownloadOnlyWhenLockHeldPollingIntervalInSecs));
            }

            uint64_t readLength = 0;
            DURATION_START(cacheDownload);
            cacheStatus = cacheClient->DownloadSync(
                m_strMountName,
                cacheFilePath,
                chunkOffset,
                0,
                m_nChunkSize,
                remoteReadData->Buffer.get(),
                readLength);
            if (cacheStatus == CacheStatus::SUCCESS)
            {
                LOG_DEBUG("Read file: {}, offset: {}, length: {} from cache", remotePath, chunkOffset, readLength);
                remoteReadData->BufferSize = readLength;
                m_nReadChunksRemoteCache++;
                m_vecCacheDownloadLatencies[m_nTotalCacheDownloadLatencySamples.fetch_add(1) % m_nLatencySamplesVecSize] = DURATION_MS_GET(cacheDownload);
            }
            else if (cacheStatus == CacheStatus::INTERNAL_ERROR )
            {
                LOG_WARN("Got server internal error for cache key: {}, ignore and continue blob download", cacheFilePath);
            }
            else if (cacheStatus == CacheStatus::CHECKSUM_MISMATCH)
            {
                LOG_WARN("Got checksum mismatch for cache key: {}, ignore and continue blob download", cacheFilePath);
            }

            nIterations++;
        }
        while (m_bRemoteDownloadOnlyWhenLockHeldEnabled &&
                cacheStatus == CacheStatus::NOTFOUND &&
                nIterations < m_nRemoteDownloadOnlyWhenLockHeldMaxAttempts);
        
        if (cacheStatus == CacheStatus::NOTFOUND && nIterations >= m_nRemoteDownloadOnlyWhenLockHeldMaxAttempts)
        {
            LOG_WARN("Failed to acquire lock/entry for cache key: {}, max attempts reached: {}, downloading from remote store",
                cacheFilePath, m_nRemoteDownloadOnlyWhenLockHeldMaxAttempts);
        }
    }

    if (cacheStatus != CacheStatus::SUCCESS)
    {
        // Fetch from remote storage   
        DURATION_START(remoteDownload);
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

        m_vecRemoteDownloadLatencies[m_nTotaRemotelDownloadLatencySamples.fetch_add(1) % m_nLatencySamplesVecSize] = DURATION_MS_GET(remoteDownload);
        LOG_DEBUG("Read file: {}, offset: {}, length: {} from remote server",
            remotePath, chunkOffset, nBytesFromRemote);
        remoteReadData->BufferSize = nBytesFromRemote;
        m_nReadChunksRemoteClient++;
    }

    // upload to cache server if this client owns the cache key
    if (cacheClient && cacheStatus == CacheStatus::NOTFOUND_LOCKED)
    {
        bool bRet = cacheClient->Upload(m_strMountName, cacheFilePath, chunkOffset, remoteReadData);
        if (!bRet)
        {
            // Not taking any action on failure as this is only a best effort service
            LOG_DEBUG("Failed to upload file: {}, offset: {} to cache", remotePath, chunkOffset);
        }
    }
    
    // Put to pre-fetch cache
    PrefetchCache::GetInstance().Put(cacheFilePath, remoteReadData);

    return remoteReadData;
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::EnsurePrefetchCacheWindowPopulated(
    const std::string& remotePath,
    size_t readSize,
    off64_t readOffset,
    off64_t fileSize)
{    
    const std::string cacheFilePath = GetCacheFilePath(remotePath);
    off64_t startWindowOffset = readOffset - (readOffset % m_nPrefetchWindowSize);
    off64_t endOffset = readOffset + readSize;
    off64_t endWindowOffset = endOffset - (endOffset % m_nPrefetchWindowSize);

    const std::set<off64_t> cachedOffsets = PrefetchCache::GetInstance().GetCachedOffsets(cacheFilePath);

    // check cached offsets to see how much of prefetch window(s) is already in cache
    off64_t prefetchStart = startWindowOffset;
    off64_t prefetchEnd = min(off64_t(endWindowOffset + m_nPrefetchWindowSize), fileSize);
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
        uint32_t nPurgeNeeded = PrefetchCache::GetInstance().MAX_ENTRIES_PER_FILE -
            (cachedOffsets.size() + uncachedOffsets.size());
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

        PrefetchCache::GetInstance().PurgeOffsets(cacheFilePath, offsetsToPurge);
    }

    // Download the uncached chunks parallely in order to populate into pre-fetch cache
    std::vector<std::future<std::shared_ptr<const FileReadData>>> asyncReadChunks;

    LOG_DEBUG("Starting parallel async read chunk calls for populating PrefetchCache."
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
void StreamingClient::UpdateReadStats(size_t readSize, bool finalRead)
{
    if (readSize <= 0)
    {
        return;
    }

    const int readIndex = finalRead ? 1 : 0;
    m_nReadFileTotal[readIndex]++;
    m_nReadTotalBytes[readIndex] += readSize;

    if (readSize <= 128 * 1024)
    {
        m_nRead128K[readIndex]++;
    }
    else if (readSize <= 4 * 1024 * 1024)
    {
        m_nRead4MB[readIndex]++;
    }
    else if (readSize <= 100 * 1024 * 1024)
    {
        m_nRead100MB[readIndex]++;
    }
    else if (readSize <= 1024 * 1024 * 1024)
    {
        m_nRead1GB[readIndex]++;
    }
    else
    {
        m_nReadLarge[readIndex]++;
    }

    // Loop to guard against concurrent update to m_nReadMax. 
    // This should go upto a few iterations only in rare scenarios so while(true) shouldn't be alarming
    uint64_t currentMax = 0;
    do
    {
        currentMax = m_nReadMax[readIndex];
    }
    while (readSize > currentMax && !m_nReadMax[readIndex].compare_exchange_strong(currentMax, readSize));
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::LogReadIO(
    const std::string& remotePath,
    off64_t readOffset,
    size_t requestedSize,
    ssize_t actualSize)
{
    if(actualSize < 0)
    {
        return ;
    }

    if (m_enableReadIOLogs)
    {
        LOG_INFO("ReadIO mountName={} path={} offset={} readSize={} actualSize={}",
            m_strMountName, remotePath, readOffset, requestedSize, actualSize);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::LongRunningLogStats(const uint32_t sleepTimeInSec)
{   
    std::unique_lock<std::mutex> lock(m_loggingMutex);
    while (!m_bStopLogging)
    {
        LogStats();
        m_loggingCV.wait_for(lock, std::chrono::seconds(sleepTimeInSec));
    }    
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::LogStats()
{
    LogReadChunkStats();
    LogGetPropertiesStats();
    LogDownloadLatencyStats();
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::LogReadChunkStats()
{
    LOG_INFO("ReadChunk stats: MountName={} Total={} PrefetchCache={} RemoteCache={} RemoteClient={}",
        m_strMountName,
        m_nReadChunksTotal,
        m_nReadChunksPrefetchCache,
        m_nReadChunksRemoteCache,
        m_nReadChunksRemoteClient);

    LOG_INFO("ReadFile requested stats. TotalReads={} 128k={} 4MB={}, 100MB={} 1GB={} >1GB={} TotalBytes={} "
        "MaxReadBytes={}",
        m_nReadFileTotal[0],
        m_nRead128K[0],
        m_nRead4MB[0],
        m_nRead100MB[0],
        m_nRead1GB[0],
        m_nReadLarge[0],
        m_nReadTotalBytes[0],
        m_nReadMax[0]);

    LOG_INFO("ReadFile serviced stats. TotalReads={} 128k={} 4MB={}, 100MB={} 1GB={} >1GB={} TotalBytes={} "
        "MaxReadBytes={}",
        m_nReadFileTotal[1],
        m_nRead128K[1],
        m_nRead4MB[1],
        m_nRead100MB[1],
        m_nRead1GB[1],
        m_nReadLarge[1],
        m_nReadTotalBytes[1],
        m_nReadMax[1]);
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::LogDownloadLatencyStats()
{
    unsigned nNumCachdeDownloadLatencySamples =
        std::min((uint64_t)m_nTotalCacheDownloadLatencySamples, (uint64_t)m_nLatencySamplesVecSize);
    unsigned nNumRemoteDownloadLatencySamples =
        std::min((uint64_t)m_nTotaRemotelDownloadLatencySamples, (uint64_t)m_nLatencySamplesVecSize);

    LogDownloadLatencyStatsInner(m_vecCacheDownloadLatencies, nNumCachdeDownloadLatencySamples, "Cache");
    LogDownloadLatencyStatsInner(m_vecRemoteDownloadLatencies, nNumRemoteDownloadLatencySamples, "Remote");
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::LogDownloadLatencyStatsInner(const std::vector<unsigned>& vecLatencies,
                                                   unsigned nNumSamples,
                                                   const char* strSource)
{
    if (nNumSamples == 0)
    {
        return;
    }

    std::vector<unsigned> vecLatenciesCopy(nNumSamples, UINT_MAX);
    std::copy_n(vecLatencies.begin(), nNumSamples, vecLatenciesCopy.begin());
    std::sort(vecLatenciesCopy.begin(), vecLatenciesCopy.end());

    unsigned nAvgLatency = std::accumulate(vecLatenciesCopy.begin(), vecLatenciesCopy.end(), 0ULL) / nNumSamples;
    LOG_INFO("Download latencies (ms) from {} MountName={} Samples={} Min={} Max={} Avg={} P50={} P95={}",
                strSource,
                m_strMountName,
                nNumSamples,
                vecLatenciesCopy.front(),
                vecLatenciesCopy.back(),
                nAvgLatency,
                vecLatenciesCopy[nNumSamples * 0.5],
                vecLatenciesCopy[nNumSamples * 0.95]);
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::LogGetPropertiesStats()
{
    LOG_INFO("GetProperties stats: MountName={} Total={} CacheHit={} CacheMiss={}",
        m_strMountName,
        m_nGetPropertiesTotal,
        m_nGetPropertiesCache,
        m_nGetPropertiesRemoteClient);
}
//---------------------------------------------------------------------------------------------------------------------
std::string StreamingClient::GetCacheFilePath(const std::string& path) const
{
    if ((StorageInterceptConfig::GetInstance().GetRemoteStoreType(m_strMountName) ==
     StorageInterceptConstants::ConfigValRemoteTypeBlob) &&
     StorageInterceptConfig::GetInstance().IsDynamicAccountMount(m_strMountName) &&
     StorageInterceptConfig::GetInstance().IsIgnoreAccountNameInCacheKey(m_strMountName))
    {
        // For dynamic mounts, we want to skip the account hostname in first component of path
        return std::string(StorageInterceptConstants::CacheKeyAccountNameOverride) + "/" + StorageInterceptUtils::RemoveFirstComponentFromPath(path);
    }

    return m_strRemotePathPrefix + path;
}
//---------------------------------------------------------------------------------------------------------------------
