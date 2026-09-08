/// Copyright (c) Microsoft Corporation.
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

#include "StreamingClient.h"
#include "RemoteClientProvider.h"
#include "CacheUtils.h"
#include "InMemoryAttributesCache.h"
#include "PrefetchCache.h"
#include "UploadCache.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptUtils.h"
#include "RemoteClientException.h"
#include <cstring>
#include <algorithm>
#include <numeric>
#include <pthread.h>
#include <fcntl.h>
#include <thread>
#include <limits>
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
    , m_nSubChunkReadThresholdPercent(StorageInterceptConfig::GetInstance().GetSubChunkReadThresholdPercent())
    , m_bEncryptionEnabled(StorageInterceptConfig::GetInstance().GetCacheEncryptionEnabled(mountName))
    , m_bChecksumValidationEnabled(StorageInterceptConfig::GetInstance().IsCacheValidateChecksumEnabled())
    , m_bDisableSubChunkForChecksumValidation(StorageInterceptConfig::GetInstance().IsSubChunkDisabledForChecksumValidation())
    , m_bEnableChunkOpParallelism(
        StorageInterceptConfig::GetInstance().GetChunkOperationParallelismEnabled(mountName))
    , m_nMaxChunkOpConcurrency(StorageInterceptConfig::GetInstance().GetChunkOperationMaxConcurrency(mountName))
    , m_bEnableLargePrefetch(StorageInterceptConfig::GetInstance().GetEnableLargePrefetch(mountName))
    , m_nPrefetchWindowSize(StorageInterceptConfig::GetInstance().GetPrefetchWindowSizeInMB(mountName) * (1*1024*1024))
    , m_bRemoteDownloadOnlyWhenLockHeldEnabled(StorageInterceptConfig::GetInstance().IsRemoteDownloadOnlyWhenLockHeldEnabled())
    , m_nRemoteDownloadOnlyWhenLockHeldPollingIntervalInSecs(
        StorageInterceptConfig::GetInstance().GetRemoteDownloadOnlyWhenLockHeldPollingIntervalInSecs())
    , m_bEnableWrite(StorageInterceptConfig::GetInstance().IsWriteSupportEnabled())
    , m_bEnableRemoteWrite(StorageInterceptConfig::GetInstance().IsRemoteWriteEnabled())
    , m_bEnableRemoteWriteAsync(StorageInterceptConfig::GetInstance().IsRemoteWriteAsyncEnabled())
    , m_stopRemoteAsyncUpload(false)
    , m_nRemoteDownloadOnlyWhenLockHeldMaxAttempts(50)
    , m_nCacheBytesWritten(0)
    , m_nRemoteBytesWritten(0)
    , m_nHighPriorityChunkOpsPending(0)
    , m_nChunkOpsInProgress(0)
    , m_nReadChunksTotal(0)
    , m_nReadChunksPrefetchCache(0)
    , m_nReadChunksRemoteCache(0)
    , m_nReadChunksRemoteClient(0)
    , m_nReadChunksZeroCopy(0)
    , m_nReadChunksSubChunk(0)
    , m_nWriteChunksTotalStarted(0)
    , m_nWriteChunksTotalCompleted(0)
    , m_nWriteChunksRemoteCache(0)
    , m_nWriteChunksRemoteStoreSync(0)
    , m_nWriteChunksRemoteStoreAsync(0)
    , m_nWriteChunksZeroCopy(0)
    , m_nGetPropertiesTotal(0)
    , m_nGetPropertiesCache(0)
    , m_nGetPropertiesRemoteClient(0)
    , m_nLastLoggedReadChunksTotal(0)
    , m_nLastLoggedWriteChunksTotal(0)
    , m_vecCacheDownloadLatencies(m_nLatencySamplesVecSize, UINT_MAX)
    , m_vecRemoteDownloadLatencies(m_nLatencySamplesVecSize, UINT_MAX)
    , m_nTotalCacheDownloadLatencySamples(0)
    , m_nTotaRemotelDownloadLatencySamples(0)
    , m_nReadFileTotal{}
    , m_nReadTotalBytes{}
    , m_nRead128K{}
    , m_nRead4MB{}
    , m_nRead100MB{}
    , m_nRead1GB{}
    , m_nReadLarge{}
    , m_nReadMax{}
    , m_bStopLogging(false)
    , m_bEnableZeroCopy(StorageInterceptConfig::GetInstance().GetEnableZeroCopy(mountName))
    , m_bEnableVersionedCacheKeys(StorageInterceptConfig::GetInstance().IsVersionedCacheKeysEnabled())
    , m_bEnableConsistentMode(StorageInterceptConfig::GetInstance().IsConsistentModeEnabled(mountName))
    , m_bEnableGarbageCollectionInConsistentMode(StorageInterceptConfig::GetInstance().IsGarbageCollectionInConsistentModeEnabled(mountName))
{
    SetClients(cacheClient, attributesCache);

    STORAGE_ASSERT(m_nChunkSize % FileData::BufferAlignmentSize == 0,
        "Chunk size must be a multiple of buffer alignment size");

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

    if (m_bEnableConsistentMode)
    {
        // TODO: Remove versioned keys check once the enableVersionedCacheKeys flag is retired.
        STORAGE_ASSERT(m_bEnableVersionedCacheKeys,
            "Consistent mode requires enableVersionedCacheKeys=true");
        if (m_bEnableWrite)
        {
            STORAGE_ASSERT(m_bEnableRemoteWrite,
                "Consistent mode does not support write-cache-only (enableWriteRemote must be true)");
            STORAGE_ASSERT(!m_bEnableRemoteWriteAsync,
                "Consistent mode does not support async remote write (enableRemoteWriteAsync must be false)");
        }
    }

    if (m_bEnableWrite && m_bEnableRemoteWrite && m_bEnableRemoteWriteAsync)
    {    
        STORAGE_ASSERT(
            !(m_spCacheClient == nullptr),
            "Async remote write requires cache to be enabled");
            const uint32_t nRemoteAsyncWriteFileConcurrency =
                StorageInterceptConfig::GetInstance().GetRemoteAsyncWriteFileConcurrency();
        LOG_INFO("Starting {} remote async upload worker threads", nRemoteAsyncWriteFileConcurrency);
        for (uint32_t i = 0; i < nRemoteAsyncWriteFileConcurrency; i++)
        {
            m_remoteAsyncUploadThreads.push_back(std::make_unique<std::thread>([this]() {
                this->WriteRemoteAsyncUploadWorker();
            }));
        }
    }

    LOG_INFO("StreamingClient initialized. MountName={}, RemotePathPrefix={} ChunkSize={}, ChunkCache={},"
             " AttributeCache={} chunkParallelism={}, maxChunkConcurrency={},"
             " subChunkReadThresholdPercent={}, encryptionEnabled={}, checksumValidationEnabled={}, disableSubChunkForChecksumValidation={},"
             " enableWrite={}, enableRemoteWrite={}, enableRemoteWriteAsync={},"
             " enableLargePrefetch={}, prefetchWindowSizeInBytes={},"
             " enableZeroCopy={} Commit hash={} built on {}",
        mountName,
        m_strRemotePathPrefix,
        m_nChunkSize,
        (m_spCacheClient ? "enabled" : "disabled"),
        (m_spAttributesCache ? "enabled" : "disabled"),
        m_bEnableChunkOpParallelism,
        m_nMaxChunkOpConcurrency,
        m_nSubChunkReadThresholdPercent,
        m_bEncryptionEnabled,
        m_bChecksumValidationEnabled,
        m_bDisableSubChunkForChecksumValidation,
        m_bEnableWrite,
        m_bEnableRemoteWrite,
        m_bEnableRemoteWriteAsync,
        m_bEnableLargePrefetch,
        m_nPrefetchWindowSize,
        m_bEnableZeroCopy,
        BUILD_GIT_COMMIT_HASH,
        __DATE__);
}
//---------------------------------------------------------------------------------------------------------------------
StreamingClient::~StreamingClient()
{
    if(m_bEnableWrite && m_bEnableRemoteWrite && m_bEnableRemoteWriteAsync)
    {
        LOG_INFO("Stopping remote uploader thread");
        m_stopRemoteAsyncUpload = true;
        m_cvQueue.notify_all();
        for(auto& thread : m_remoteAsyncUploadThreads)
        {
            thread->join();
        }
    }

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

    if (StorageInterceptConfig::GetInstance().ShouldEnableAttributesCache(m_strMountName))
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
    void *pUserBuf,
    size_t readSize,
    off64_t readOffset,
    off64_t fileSize,
    const std::string& version)
{
    LOG_DEBUG("ReadFile: remotePath={} version={} readOffset={} readSize={}",
        remotePath, version, readOffset, readSize);

    if (readOffset < 0 || fileSize < 0)
    {
        LOG_ERROR("ReadFile received an invalid offset or file size. remotePath={} readOffset={} fileSize={}",
            remotePath, readOffset, fileSize);
        errno = EINVAL;
        return -1;
    }

    UpdateReadStats(readSize, false);

    if (readOffset >= fileSize)
    {
        return 0; // EOF reached.
    }

    const size_t availableBytes = static_cast<size_t>(fileSize - readOffset);
    const size_t requestedReadSize = readSize;
    if (readSize > availableBytes)
    {
        // Trim readSize so that we don't trigger unnecessary reads beyond file size.
        readSize = availableBytes;
        LOG_DEBUG("ReadFile trimmed request at EOF. remotePath={} readOffset={} requestedSize={} effectiveSize={}",
            remotePath, readOffset, requestedReadSize, readSize);
    }

    if (readSize > static_cast<size_t>(std::numeric_limits<ssize_t>::max()))
    {
        LOG_ERROR("ReadFile effective size cannot be represented by ssize_t. remotePath={} readOffset={} "
            "requestedSize={} effectiveSize={}", remotePath, readOffset, requestedReadSize, readSize);
        errno = EOVERFLOW;
        return -1;
    }

    if (pUserBuf == nullptr && readSize > 0)
    {
        LOG_ERROR("ReadFile received a null buffer. remotePath={} readOffset={} readSize={}",
            remotePath, readOffset, readSize);
        errno = EFAULT;
        return -1;
    }

    // If large prefetch is enabled and read size less than prefetch window size,
    // prefetch the window first and then read and return.
    //
    // Note: Large Prefetch optimization is only helpful in case of small sequential reads on a large file.
    if (m_bEnableLargePrefetch
        && readSize < m_nPrefetchWindowSize)
    {
        EnsurePrefetchCacheWindowPopulated(remotePath, readSize, readOffset, fileSize, version);

        // All data for this read is now in prefetchCache,
        // reading chunks parallely won't speed-up as there won't be remote store calls.
        ssize_t readBytes = ReadChunksSequential(remotePath, pUserBuf, readSize, readOffset, version);
        UpdateReadStats(readBytes, true);
        LogReadIO(remotePath, readOffset, readSize, readBytes);
        return readBytes;
    }

    // If parallel chunk downloads are enabled and readSize is larger than chunk size,
    // read required chunks in parallel.
    if (m_bEnableChunkOpParallelism && readSize > m_nChunkSize)
    {
        ssize_t readBytes = ReadChunksParallel(remotePath, pUserBuf, readSize, readOffset, version);
        UpdateReadStats(readBytes, true);
        LogReadIO(remotePath, readOffset, readSize, readBytes);
        return readBytes;
    }

    ssize_t readBytes = ReadChunksSequential(remotePath, pUserBuf, readSize, readOffset, version);
    UpdateReadStats(readBytes, true);
    LogReadIO(remotePath, readOffset, readSize, readBytes);
    return readBytes;
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StreamingClient::WriteFile(
    /*[in] */ const std::string& remotePath,
    /*[in] */ const void *buf,
    /*[in] */ size_t writeSize,
    /*[in] */ off64_t writeOffset)
{
    if (writeSize > static_cast<size_t>(std::numeric_limits<ssize_t>::max()))
    {
        LOG_ERROR("WriteFile request size cannot be represented by ssize_t. remotePath={} writeOffset={} writeSize={}",
            remotePath, writeOffset, writeSize);
        errno = EOVERFLOW;
        return -1;
    }

    if (writeOffset < 0)
    {
        LOG_ERROR("WriteFile received a negative offset. remotePath={} writeOffset={} writeSize={}",
            remotePath, writeOffset, writeSize);
        errno = EINVAL;
        return -1;
    }

    if (writeSize > static_cast<size_t>(std::numeric_limits<off64_t>::max() - writeOffset))
    {
        LOG_ERROR("WriteFile offset and size exceed the supported file range. remotePath={} writeOffset={} writeSize={}",
            remotePath, writeOffset, writeSize);
        errno = EOVERFLOW;
        return -1;
    }

    if(!m_bEnableWrite)
    {
        LOG_ERROR("Write operation not enabled");
        return -1;
    }

    if(buf == nullptr && writeSize > 0)
    {
        LOG_ERROR("Empty write buffer passed. remotePath={} writeOffset={} writeSize={}",
            remotePath, writeOffset, writeSize);
        errno = EFAULT;
        return -1;
    }

    // There are three scenarios based on the writeOffset that is passed
    // 1. writeOffset == fileSize: simple case, append
    // 2. writeOffset < fileSize: overwrite case, not supported yet
    // 3. writeOffset > fileSize - sparse file case, add zeroed out data from fileSize to writeOffset
    const std::string cacheFilePath = GetCacheFilePath(remotePath);
    const size_t fileSize = UploadCache::GetInstance().GetFileSize(cacheFilePath);

    if (writeOffset < fileSize)
    {
        LOG_ERROR("Overwrite writes not supported yet. remotePath={} writeOffset={} fileSize={}",
            remotePath,
            writeOffset,
            fileSize);
        errno = EOPNOTSUPP;  // Operation not supported
        return -1;
    }

    ssize_t bytesWritten = 0;
    while(bytesWritten < writeSize)
    {
        const off64_t fileWriteLocation = writeOffset + bytesWritten;
        const off64_t curChunkFileOffset = fileWriteLocation - (fileWriteLocation % m_nChunkSize);
        const off64_t curChunkLocalOffset = fileWriteLocation - curChunkFileOffset;
        // How many bytes to write out of this chunk starting at the offset within the chunk
        // Or if we have fewer bytes left to write than the chunk size, write only those
        const off64_t bytesToWrite = std::min(
            (std::size_t)m_nChunkSize - curChunkLocalOffset,
            writeSize - bytesWritten);
        const bool isFullChunkWrite = (bytesToWrite == m_nChunkSize) && (curChunkLocalOffset == 0);
        
        void* ptr = (void*)((char*)buf + bytesWritten);

        std::shared_ptr<FileData> writeData = UploadCache::GetInstance().Get(cacheFilePath, curChunkFileOffset);
        if(writeData == nullptr)
        {
            LOG_DEBUG("Creating new upload cache chunk. remotePath={} chunkOffset={} isFullChunkWrite={}",
                remotePath,
                curChunkFileOffset,
                isFullChunkWrite);
            writeData = std::make_shared<FileData>(
                curChunkFileOffset,
                m_nChunkSize,
                isFullChunkWrite ? (char*)ptr : nullptr, // use zero copy for full chunk writes
                !isFullChunkWrite /* setToZeros */);
            UploadCache::GetInstance().Put(cacheFilePath, writeData, curChunkFileOffset + bytesToWrite);

            m_nWriteChunksTotalStarted++;
            if (isFullChunkWrite)
            {
                m_nWriteChunksZeroCopy++;
            }
        }

        // Write the contents into the upload cache chunk (if not using zero copy)
        if (!isFullChunkWrite)
        {
            std::memcpy(writeData->Buffer.get() + curChunkLocalOffset, ptr, bytesToWrite);
        }
        bytesWritten += bytesToWrite;
    }

    // Update file size to reflect the full extent of data written.
    // Put() only updates file size when a new chunk is created, but writes to
    // existing chunks can extend the file beyond the previously tracked size.
    UploadCache::GetInstance().UpdateFileSize(cacheFilePath, writeOffset + bytesWritten);

    return bytesWritten;
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StreamingClient::ReadChunksSequential(
    const std::string& remotePath,
    void *pUserBuf,
    size_t totalReadSize,
    off64_t readOffset,
    const std::string& version)
{
    ssize_t nBytesRead = 0;
    const size_t readStart = static_cast<size_t>(readOffset);
    const size_t readEnd = readStart + totalReadSize;

    // Divide into chunks and read sequentially
    size_t chunkOffset = readStart - (readStart % m_nChunkSize);
    size_t bufOffset = 0;
    while(chunkOffset < readEnd)
    {
        // Proceed only when we are not exceeding the max allowed concurrency.
        PreChunkOpCheck(false /* isLowPriority */);

        const size_t currentOffset = readStart + bufOffset;

        const size_t chunkEnd = std::min(readEnd, chunkOffset + m_nChunkSize);
        const size_t bufSizeForChunkCopy = chunkEnd - currentOffset;
        const size_t chunkCopyOffset = currentOffset - chunkOffset;

        LOG_DEBUG("ReadChunksSequential. remotePath={} chunkOffset={} currentOffset={} chunkCopyOffset={}"
            " bufSizeForChunkCopy={} bufOffset={} readSize={} readEnd={}",
            remotePath,
            chunkOffset,
            currentOffset, 
            chunkCopyOffset,
            bufSizeForChunkCopy,
            bufOffset,
            totalReadSize,
            readEnd);

        ssize_t bytesReadIntoBuffer = ReadChunkAndCopyToBuffer(
                remotePath,
            static_cast<off64_t>(chunkOffset),
                (char*)pUserBuf + bufOffset,
                bufSizeForChunkCopy,
                chunkCopyOffset,
                version);

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
    void *pUserBuf,
    size_t totalReadSize,
    off64_t readOffset,
    const std::string& version)
{
    const size_t readStart = static_cast<size_t>(readOffset);
    const size_t readEnd = readStart + totalReadSize;

    // Divide into chunks and read parallely
    size_t chunkOffset = readStart - (readStart % m_nChunkSize);
    size_t bufOffset = 0;
    std::vector<AsyncReadChunkInfo> asyncReadChunks;

    while(chunkOffset < readEnd)
    {
        // Proceed only when we are not exceeding the max allowed concurrency
        PreChunkOpCheck(false /* isLowPriority */);

        const size_t currentOffset = readStart + bufOffset;

        const size_t chunkEnd = std::min(readEnd, chunkOffset + m_nChunkSize);
        const size_t bufSizeForChunkCopy = chunkEnd - currentOffset;
        const size_t chunkCopyOffset = currentOffset - chunkOffset;

        LOG_DEBUG("ReadChunksParallel. remotePath={} chunkOffset={} currentOffset={} chunkCopyOffset={} bufSizeForChunkCopy={} bufOffset={} readSize={} readEnd={} buf={}",
            remotePath,
            chunkOffset,
            currentOffset,
            chunkCopyOffset,
            bufSizeForChunkCopy,
            bufOffset,
            totalReadSize,
            readEnd,
            static_cast<void*>(static_cast<char*>(pUserBuf) + bufOffset));
        // start async read chunk and store future result.
        asyncReadChunks.push_back(AsyncReadChunkInfo(chunkOffset, bufSizeForChunkCopy));
        // TODO: use threadpool to avoid thread creation costs.
        asyncReadChunks.back().AsyncResult = std::async(
            std::launch::async,
            &StreamingClient::ReadChunkAndCopyToBuffer,
            this,
            std::ref(remotePath),
            static_cast<off64_t>(chunkOffset),
            (char*)pUserBuf + bufOffset,
            bufSizeForChunkCopy,
            chunkCopyOffset,
            std::ref(version));

        bufOffset += bufSizeForChunkCopy;
        chunkOffset += m_nChunkSize;
    }

    // Wait for all the async read chunks to complete
    LOG_DEBUG("Collecting the results of all read chunk calls. RemotePath={} ReadOffset={} ReadSize={} ReadChunks={}",
        remotePath,
        readOffset,
        totalReadSize,
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

        LOG_DEBUG("ReadChunkAndCopyToBuffer completed. remotePath={} chunkOffset={}",
            remotePath,
            asyncReadChunk.ChunkOffset);
        nBytesRead += bytesRead;
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
void StreamingClient::Close(
    const std::string& remotePath,
    const std::string& existingVersion,
    const bool isWriteMode)
{
    // existingVersion carries the file's version at open time (empty for new files)
    PrefetchCache::GetInstance().PurgeAll(GetCacheFilePath(remotePath), existingVersion);

    // If the file was opened in write mode, we need to do two additional things
    // 1. Upload the chunks to the cache server if caching is enabled
    // 2. Upload the chunks to the remote store if enabled and sync\async depending on config
    if(isWriteMode)
    {
        STORAGE_ASSERT(
            m_bEnableWrite,
            "Close called in write mode when write support is not enabled");

        const off64_t fileSize = UploadCache::GetInstance().GetFileSize(GetCacheFilePath(remotePath));

        // Generate a new version for write (when versioned cache keys are enabled)
        std::string newWriteVersion;
        if (m_bEnableVersionedCacheKeys)
        {
            newWriteVersion = StorageInterceptUtils::GenerateUUID();
        }
        LOG_INFO("Generated write version={} for remotePath={} isVersionedKeysEnabled={}",
            newWriteVersion, remotePath, m_bEnableVersionedCacheKeys);

        struct timespec curTimeSpec;
        clock_gettime(CLOCK_REALTIME, &curTimeSpec);
        FileAttributes postWriteFileAttributes;
        postWriteFileAttributes.isDir = false;
        postWriteFileAttributes.size = fileSize;
        postWriteFileAttributes.mTime = curTimeSpec;
        postWriteFileAttributes.aTime = curTimeSpec;
        postWriteFileAttributes.version = newWriteVersion;        
        LOG_INFO("Closing file in write mode. remotePath={} fileSize={}", remotePath, fileSize);

        if(!WriteChunksToCache(remotePath, postWriteFileAttributes))
        {
            LOG_ERROR("Failed to write chunks to cache, path={}", remotePath);
            // Treat as fatal error if sync blob write is not enabled
            if (!(m_bEnableRemoteWrite && !m_bEnableRemoteWriteAsync))
            {
                throw RemoteClientException("CacheError: Failed to write chunks to cache");
            }
        }

        if(!WriteChunksToRemoteStore(remotePath, fileSize, newWriteVersion))
        {
            // Treat blob write error as fatal.
            // Note: The data may have been written to cache successfully, if cache write is enabled.
            LOG_ERROR("Failed to write chunks to remote store, path={}", remotePath);
            throw RemoteClientException("RemoteError: Failed to write chunks to remote store");
        }

        // Purge the whole file from upload cache
        UploadCache::GetInstance().Purge(GetCacheFilePath(remotePath));

        // Evict stale cache chunks for the version that was on disk before this write
        LOG_DEBUG("Attempting Eviction of existing version cache after version update."
            " remotePath={} existingVersion={} newVersion={}",
            remotePath,
            existingVersion,
            newWriteVersion);
        if (m_bEnableGarbageCollectionInConsistentMode)
        {
            EvictCacheForFileVersion(remotePath, existingVersion);
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::EvictCacheForFileVersion(
    const std::string& remotePath,
    const std::string& version)
{
    if (version.empty())
    {
        LOG_DEBUG("Version is empty, skipping cache eviction. remotePath={}", remotePath);
        return;
    }

    auto cacheClient = std::atomic_load(&m_spCacheClient);
    if (!cacheClient)
    {
        LOG_DEBUG("Cache client is not available, skipping cache eviction. remotePath={}", remotePath);
        return;
    }

    const std::string groupId = GetGroupIdForFileVersion(remotePath, version);
    LOG_DEBUG("Evicting cache for version. remotePath={} version={} groupId={}",
        remotePath, version, groupId);
    cacheClient->DeleteGroup(groupId);
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StreamingClient::ReadChunkAndCopyToBuffer(
    const std::string& remotePath,
    off64_t chunkOffset,
    void* pUserBuf,
    size_t bufSize,
    size_t chunkCopyOffset,
    const std::string& version)
{
    // Sub-chunk fast path: for small reads on cache hits, request only the needed bytes
    // instead of fetching the entire chunk and copying a slice.
    //
    // Despite better perf due to extra read, there are negative implications of sub-chunk reads:
    // 1. Checksum validation is not supported for sub-chunk reads, as the checksum is computed on the entire chunk.
    // 2. PrefetchCache is not populated for sub-chunk reads, as the entire chunk is not fetched,
    //    leading to slower subsequent subchunk reads on the same chunk
    // For these reasons, we don't do sub-chunk reads if read amplification is low (m_nSubChunkReadThresholdPercent is default 80%)
    if ((bufSize * 100 / m_nChunkSize) < m_nSubChunkReadThresholdPercent
        && !m_bEncryptionEnabled
        && !(m_bChecksumValidationEnabled && m_bDisableSubChunkForChecksumValidation))
    {
        auto subChunkResult = TrySubChunkRead(remotePath, chunkOffset, pUserBuf, bufSize, chunkCopyOffset, version);
        if (subChunkResult.has_value())
        {
            return subChunkResult.value();
        }
    }

    // Zero copy should be done only if chunk copy offset is in chunkSize boundary and valid user buffer is passed.
    // And whether user buffer is aligned when ZC is enabled
    const bool bCanZeroCopyPathBeTaken = (m_bEnableZeroCopy
                                          && pUserBuf
                                          && bufSize == m_nChunkSize
                                          && chunkCopyOffset == 0);
        
    std::shared_ptr<const FileData> readChunkData = ReadChunk(remotePath,
                                                                  chunkOffset,
                                                                  bCanZeroCopyPathBeTaken ? pUserBuf : nullptr,
                                                                  version);
    if (!readChunkData)
    {
        LOG_ERROR("Read chunk failed. remotePath={} chunkOffset={}",
            remotePath,
            chunkOffset);
        return -1;
    }

    // If user buffer is filled, we have used the user buffer as zero-copy to read the data from cache or remote storage.
    // In this case, we will not copy the data into caller-provided buffer.
    if (readChunkData->bIsUserBuffer)
    {
        LOG_DEBUG("Zero-copy read. chunkOffset={} chunkCopyOffset={} bufSize={}",
            chunkOffset,
            chunkCopyOffset,
            readChunkData->BufferSize);
        return readChunkData->BufferSize;
    }
    else
    {
        // Copy from the chunk read data into caller-provided buffer if it is not a zero-copy read.
        const size_t copySize = std::min(
            readChunkData->BufferSize - chunkCopyOffset,
            bufSize);
        if (copySize > 0)
        {
            memcpy(
                (char*)pUserBuf,
                readChunkData->Buffer.get() + chunkCopyOffset,
                copySize);
        }
        LOG_DEBUG("Copied into caller buffer. chunkOffset={} chunkCopyOffset={} copySize={}",
            chunkOffset,
            chunkCopyOffset,
            copySize);
        return copySize;
    }
}
//---------------------------------------------------------------------------------------------------------------------
CacheStatus StreamingClient::ProbeCacheServerWithRetry(
    const std::string& cacheFilePath,
    off64_t chunkOffset,
    uint64_t startOffset,
    uint64_t length,
    char* buffer,
    uint64_t& readLength,
    const std::string& version)
{
    CacheStatus cacheStatus = CacheStatus::NOTFOUND;
    auto cacheClient = std::atomic_load(&m_spCacheClient);
    if (!cacheClient)
    {
        return cacheStatus;
    }

    int nIterations = 0;
    do
    {
        if (nIterations > 0)
        {
            LOG_INFO("Waiting for cache entry for cacheFilePath: {}, attempt: {}, retrying after {} seconds",
                cacheFilePath,
                nIterations,
                m_nRemoteDownloadOnlyWhenLockHeldPollingIntervalInSecs);
            std::this_thread::sleep_for(std::chrono::seconds(m_nRemoteDownloadOnlyWhenLockHeldPollingIntervalInSecs));
        }

        readLength = 0;
        DURATION_START(cacheDownload);
        cacheStatus = cacheClient->DownloadSync(
            m_strMountName,
            cacheFilePath,
            version,
            chunkOffset,
            startOffset,
            length,
            buffer,
            readLength);

        if (cacheStatus == CacheStatus::SUCCESS)
        {
            m_nReadChunksRemoteCache++;
            m_vecCacheDownloadLatencies[m_nTotalCacheDownloadLatencySamples.fetch_add(1) % m_nLatencySamplesVecSize] = DURATION_MS_GET(cacheDownload);
        }
        else if (cacheStatus == CacheStatus::INTERNAL_ERROR)
        {
            LOG_WARN("Server internal error for cacheFilePath: {}, offset: {}, startOffset: {}, length: {}, ignore and continue blob download",
                cacheFilePath, chunkOffset, startOffset, length);
        }
        else if (cacheStatus == CacheStatus::CHECKSUM_MISMATCH)
        {
            LOG_WARN("Checksum mismatch for cacheFilePath: {}, offset: {}, startOffset: {}, length: {}, ignore and continue blob download",
                cacheFilePath, chunkOffset, startOffset, length);
        }

        nIterations++;
    }
    while (m_bRemoteDownloadOnlyWhenLockHeldEnabled &&
            cacheStatus == CacheStatus::NOTFOUND &&
            nIterations < m_nRemoteDownloadOnlyWhenLockHeldMaxAttempts);

    if (cacheStatus == CacheStatus::NOTFOUND && nIterations >= m_nRemoteDownloadOnlyWhenLockHeldMaxAttempts)
    {
        LOG_WARN("Max retry attempts reached for cacheFilePath: {}, max attempts: {}, downloading from remote store",
            cacheFilePath, m_nRemoteDownloadOnlyWhenLockHeldMaxAttempts);
    }

    return cacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t StreamingClient::DownloadFullChunkFromBlob(
    const std::string& remotePath,
    off64_t chunkOffset,
    FileData& readData)
{
    STORAGE_ASSERT(chunkOffset % m_nChunkSize == 0, "DownloadFullChunkFromBlob called with unaligned chunkOffset");

    // NOTE: This function downloads into readData.Buffer regardless of ownership.
    // Callers passing a user buffer (bIsUserBuffer=true) must handle cache upload
    // separately (see ReadChunkImpl post-download logic).

    DURATION_START(remoteDownload);
    ssize_t nBytesFromRemote = std::atomic_load(&m_spRemoteClient)->ReadChunkSync(
        remotePath,
        readData.Buffer.get(),
        m_nChunkSize,
        chunkOffset);
    if (nBytesFromRemote == -1)
    {
        LOG_ERROR("Remote read failed. remotePath={} readSize={} readOffset={}",
            remotePath, m_nChunkSize, chunkOffset);
        return -1;
    }

    m_vecRemoteDownloadLatencies[m_nTotaRemotelDownloadLatencySamples.fetch_add(1) % m_nLatencySamplesVecSize] = DURATION_MS_GET(remoteDownload);
    readData.BufferSize = nBytesFromRemote;
    m_nReadChunksRemoteClient++;
    return nBytesFromRemote;
}
//---------------------------------------------------------------------------------------------------------------------
std::optional<ssize_t> StreamingClient::TrySubChunkRead(
    const std::string& remotePath,
    off64_t chunkOffset,
    void* pUserBuf,
    size_t bufSize,
    size_t chunkCopyOffset,
    const std::string& version)
{
    const std::string cacheFilePath = GetCacheFilePath(remotePath);

    // Check PrefetchCache first — memcpy from RAM is cheaper than a cache-server round-trip
    std::shared_ptr<const FileData> prefetchResult =
        PrefetchCache::GetInstance().Get(cacheFilePath, chunkOffset, version);
    if (prefetchResult && chunkCopyOffset < prefetchResult->BufferSize)
    {
        const size_t availableBytes = prefetchResult->BufferSize - chunkCopyOffset;
        const size_t copySize = std::min(bufSize, availableBytes);
        memcpy(pUserBuf, prefetchResult->Buffer.get() + chunkCopyOffset, copySize);
        m_nReadChunksSubChunk++;
        m_nReadChunksTotal++;
        m_nReadChunksPrefetchCache++;
        PostChunkOpComplete();
        LOG_DEBUG("Sub-chunk prefetch-cache hit. chunkOffset={} chunkCopyOffset={} copySize={} bufSize={}",
            chunkOffset, chunkCopyOffset, copySize, bufSize);
        return static_cast<ssize_t>(copySize);
    }

    // PrefetchCache miss — probe the cache server with sub-chunk parameters.
    // Uses the same retry loop as ReadChunkImpl via ProbeCacheServerWithRetry.
    // The only intentional difference from ReadChunkImpl is no zero-copy path — sub-chunk reads
    // require bufSize < m_nChunkSize, which is mutually exclusive with zero-copy.
    uint64_t readLength = 0;
    CacheStatus cacheStatus = ProbeCacheServerWithRetry(
        cacheFilePath, chunkOffset, chunkCopyOffset, bufSize,
        static_cast<char*>(pUserBuf), readLength, version);

    if (cacheStatus == CacheStatus::SUCCESS)
    {
        m_nReadChunksSubChunk++;
        m_nReadChunksTotal++;
        PostChunkOpComplete();
        LOG_DEBUG("Sub-chunk cache hit. chunkOffset={} chunkCopyOffset={} bufSize={} readLength={}",
            chunkOffset, chunkCopyOffset, bufSize, readLength);
        return static_cast<ssize_t>(readLength);
    }

    // Cache miss or error — download full chunk from blob (same as ReadChunkImpl)
    auto remoteReadData = std::make_shared<FileData>(
        chunkOffset, m_nChunkSize, nullptr, false /* setToZeros */);

    STORAGE_ASSERT(!remoteReadData->bIsUserBuffer,
        "TrySubChunkRead must own the download buffer for cache upload");
    ssize_t nBytesFromRemote = DownloadFullChunkFromBlob(remotePath, chunkOffset, *remoteReadData);
    if (nBytesFromRemote == -1)
    {
        PostChunkOpComplete();
        return static_cast<ssize_t>(-1);
    }

    // Upload to cache server if we hold the lock (same guard as ReadChunkImpl)
    if (cacheStatus == CacheStatus::NOTFOUND_LOCKED)
    {
        auto cacheClient = std::atomic_load(&m_spCacheClient);
        LOG_DEBUG("Sub-chunk: uploading to cache server. file={} offset={}", remotePath, chunkOffset);
        bool bRet = cacheClient->Upload(m_strMountName, cacheFilePath, version, chunkOffset, remoteReadData, false /* isWrite */);
        if (!bRet)
        {
            LOG_DEBUG("Sub-chunk: failed to upload to cache. file={} offset={}", remotePath, chunkOffset);
        }
    }

    // Populate PrefetchCache with the full chunk
    PrefetchCache::GetInstance().Put(cacheFilePath, remoteReadData, version);

    // Extract sub-chunk bytes (handle short reads near EOF)
    const size_t availableBytes = (remoteReadData->BufferSize > chunkCopyOffset)
        ? remoteReadData->BufferSize - chunkCopyOffset
        : 0;
    const size_t copySize = std::min(bufSize, availableBytes);
    if (copySize > 0)
    {
        memcpy(pUserBuf, remoteReadData->Buffer.get() + chunkCopyOffset, copySize);
    }
    m_nReadChunksSubChunk++;
    m_nReadChunksTotal++;
    PostChunkOpComplete();
    LOG_DEBUG("Sub-chunk blob fallback. chunkOffset={} chunkCopyOffset={} copySize={} fullChunkBytes={}",
        chunkOffset, chunkCopyOffset, copySize, nBytesFromRemote);
    return static_cast<ssize_t>(copySize);
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<const FileData> StreamingClient::ReadChunk(
    const std::string& remotePath,
    off64_t chunkOffset,
    void* pUserBuf,
    const std::string& version)
{
    std::shared_ptr<const FileData> result = ReadChunkImpl(remotePath,
                                                               chunkOffset,
                                                               pUserBuf,
                                                               version);

    PostChunkOpComplete();

    // Increment the stats only if the read was successful.
    if (result)
    {
        m_nReadChunksTotal++;
    }
    return result;
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<const FileData> StreamingClient::ReadChunkImpl(
    const std::string& remotePath,
    off64_t chunkOffset,
    void* pUserBuf,
    const std::string& version)
{
    if ((chunkOffset % m_nChunkSize) != 0)
    {
        LOG_ERROR("Invalid usage of readChunk. chunkOffset={} is not at chunk boundary.", chunkOffset);
        return nullptr;
    }

    const std::string cacheFilePath = GetCacheFilePath(remotePath);

    // Let's try to fetch from prefetch cache
    // Even if zero-copy is enabled, we will still try to fetch from prefetch cache
    // to avoid unnecessary remote storage/cache server calls, as memcopy is cheaper than network calls.
    std::shared_ptr<const FileData> prefetchLookupResult =
        PrefetchCache::GetInstance().Get(cacheFilePath, chunkOffset, version);
    if (prefetchLookupResult)
    {
        m_nReadChunksPrefetchCache++;
        return prefetchLookupResult;
    }

    // Create a read data object to hold the data read from cache or remote storage
    // If Zero Copy is enabled, we will use the pUserBuf as zero-copy to read the data.
    // Else as pUserBuf was NULL - allocate and fetch the contents into new buffer,
    // as this would be used for upload to cache in async manner and also held up in prefetch cache.
    std::shared_ptr<FileData> remoteReadData =
        std::make_shared<FileData>(
            chunkOffset,
            m_nChunkSize,
            (char *)pUserBuf,
            false /* setToZeros */);

    // Try fetching from cache service
    uint64_t readLength = 0;
    CacheStatus cacheStatus = ProbeCacheServerWithRetry(
        cacheFilePath, chunkOffset, 0, m_nChunkSize,
        remoteReadData->Buffer.get(), readLength, version);

    if (cacheStatus == CacheStatus::SUCCESS)
    {
        remoteReadData->BufferSize = readLength;
    }

    if (cacheStatus == CacheStatus::SUCCESS && remoteReadData->bIsUserBuffer)
    {
        // Increment the read chunk count for zero-copy read
        m_nReadChunksZeroCopy++;

        // Zero copy read done from cache into user buffer.
        // Hence returning the same buffer without upload of prefetch cache (to save memcpy)
        LOG_DEBUG("ZC Cache read. Data already cached, no need to upload.");
        return remoteReadData;
    }

    // If cache client is not available or cache download failed, fetch from remote storage
    if (cacheStatus != CacheStatus::SUCCESS)
    {
        ssize_t nBytesFromRemote = DownloadFullChunkFromBlob(remotePath, chunkOffset, *remoteReadData);
        if (nBytesFromRemote == -1)
        {
            return nullptr;
        }
        LOG_DEBUG("Read file: {}, offset: {}, length: {} from remote server",
            remotePath, chunkOffset, nBytesFromRemote);
    }

    // We have now the data in remoteReadData->Buffer, which is from remote storage.
    // Hence, we need to upload it to the cache server & prefetch cache
    std::shared_ptr<FileData> bufferForCacheUpload = remoteReadData;
    auto cacheClient = std::atomic_load(&m_spCacheClient);
    if (remoteReadData->bIsUserBuffer)
    {
        // Zero copy read done from remote store into user buffer and cache is not enabled,
        // Hence return the same buffer - there is no memcpy + upload to cache required.
        if (!cacheClient)
        {
            // Increment the read chunk count for zero-copy read
            m_nReadChunksZeroCopy++;

            LOG_DEBUG("ZC Remote read. No cache client available, no need to upload to cache server or prefetch cache");
            return remoteReadData;
        }

        // Create a new buffer for upload to cache server and prefetch cache
        bufferForCacheUpload =
            std::make_shared<FileData>(
                chunkOffset,
                remoteReadData->BufferSize,
                nullptr,
                false /* setToZeros */);

        memcpy(bufferForCacheUpload->Buffer.get(),
               remoteReadData->Buffer.get(),
               remoteReadData->BufferSize);
    }

    // upload to cache server if this client owns the cache key
    if (cacheClient && cacheStatus == CacheStatus::NOTFOUND_LOCKED)
    {
        LOG_DEBUG("Uploading to cache server. file: {}, offset: {}", remotePath, chunkOffset);
        bool bRet = cacheClient->Upload(m_strMountName, cacheFilePath, version, chunkOffset, bufferForCacheUpload, false /* isWrite */);
        if (!bRet)
        {
            // Not taking any action on failure as this is only a best effort service
            LOG_DEBUG("Failed to upload file: {}, offset: {} to cache", remotePath, chunkOffset);
        }
    }

    // Put to pre-fetch cache
    LOG_DEBUG("Uploading to prefetch cache. file: {}, offset: {}", remotePath, chunkOffset);
    PrefetchCache::GetInstance().Put(cacheFilePath, bufferForCacheUpload, version);

    return remoteReadData;
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::EnsurePrefetchCacheWindowPopulated(
    const std::string& remotePath,
    size_t readSize,
    off64_t readOffset,
    off64_t fileSize,
    const std::string& version)
{    
    const std::string cacheFilePath = GetCacheFilePath(remotePath);
    off64_t startWindowOffset = readOffset - (readOffset % m_nPrefetchWindowSize);
    off64_t endOffset = readOffset + readSize;
    off64_t endWindowOffset = endOffset - (endOffset % m_nPrefetchWindowSize);

    const std::set<off64_t> cachedOffsets = PrefetchCache::GetInstance().GetCachedOffsets(cacheFilePath, version);

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

        PrefetchCache::GetInstance().PurgeOffsets(cacheFilePath, offsetsToPurge, version);
    }

    // Download the uncached chunks parallely in order to populate into pre-fetch cache
    std::vector<std::future<std::shared_ptr<const FileData>>> asyncReadChunks;

    LOG_DEBUG("Starting parallel async read chunk calls for populating PrefetchCache."
        " RemotePath={} PrefetchStart={} PrefetchEnd={} UncachedOffsets={} MaxConcurrency={}"
        " ReadStartOffset={} ReadEndOffset={} m_nChunkSize={} readSize={}",
        remotePath,
        prefetchStart,
        prefetchEnd,
        uncachedOffsets.size(),
        m_nMaxChunkOpConcurrency,
        readOffset,
        endOffset,
        m_nChunkSize,
        readSize);

    // Iterate through the uncached offsets and start async read chunk calls
    // to populate the prefetch cache.
    for (off64_t chunkOffset : uncachedOffsets)
    {
        // Proceed only when we are not exceeding the max allowed concurrency
        PreChunkOpCheck(false /* isLowPriority */);

        // start async read chunk and store future result.
        // Irrespective of enabling ZeroCopy Path, keeping this logic untouched
        // as this will help to populate the prefetch cache and cache servers,
        // which will be used for subsequent reads. Hence disable zero-copy for this.
        asyncReadChunks.push_back(std::async(
            std::launch::async,
            &StreamingClient::ReadChunk,
            this,
            std::ref(remotePath),
            chunkOffset,
            nullptr, // pUserBuf is NULL as we are not using it for prefetch cache population.
            std::ref(version)));
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
        std::shared_ptr<const FileData> readChunkResult = asyncReadChunk.get();
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
void StreamingClient::PreChunkOpCheck(bool isLowPriority)
{
    LOG_DEBUG("Enter PreChunkOpCheck. m_nChunkOpsInProgress={} highPriorityPending={}",
        m_nChunkOpsInProgress,
        m_nHighPriorityChunkOpsPending);
    if (m_nChunkOpsInProgress >= m_nMaxChunkOpConcurrency)
    {
        std::unique_lock<std::mutex> lock(m_mutexChunkOps);
        if (!isLowPriority)
        {
            m_nHighPriorityChunkOpsPending++;
        }
        m_cvChunkOps.wait(lock,
            [this, isLowPriority]
            {
                int spareSlots = m_nMaxChunkOpConcurrency - this->m_nChunkOpsInProgress;
                if (spareSlots > 0)
                {
                    if (isLowPriority)
                    {
                        // Low priority ops proceed only if there are more spare slots than high priority ops waiting.
                        return spareSlots > m_nHighPriorityChunkOpsPending;
                    }

                    // High priority op gets a slot; decrease pending count (guard against underflow).
                    if (m_nHighPriorityChunkOpsPending > 0) {
                        this->m_nHighPriorityChunkOpsPending--;
                    }
                    return true;
                }
                return false; // Continue waiting if no spare slots.
            });
    }

    m_nChunkOpsInProgress++;
    LOG_DEBUG("Exit PreChunkOpCheck. m_nChunkOpsInProgress={}", m_nChunkOpsInProgress);
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::PostChunkOpComplete()
{
    // No need to lock as we are using atomic for m_nChunkOpsInProgress
    m_nChunkOpsInProgress--;
    m_cvChunkOps.notify_one();
}
//---------------------------------------------------------------------------------------------------------------------
bool StreamingClient::WriteChunksToCache(const std::string& remotePath, const FileAttributes& attributes)
{
    auto cacheClient = std::atomic_load(&m_spCacheClient);
    const std::string cacheFilePath = GetCacheFilePath(remotePath);
    const off64_t fileSize = attributes.size;
    const std::string& version = attributes.version;

    // Always update attributes cache even if cache client is not configured
    // This ensures GetProperties returns correct values for newly written files
    auto attributesCache = std::atomic_load(&m_spAttributesCache);
    if(attributesCache)
    {
        // This is best effort, okay if failures occur
        attributesCache->Put(cacheFilePath, attributes);
        LOG_DEBUG("Updated attributes cache for newly written file: {}, size={}", remotePath, fileSize);
    }

    if (!cacheClient)
    {
        LOG_DEBUG("Cache client not configured, skipping chunk upload to cache for path={}", remotePath);
        return true; // No-op for cache upload, but attributes cache was updated
    }

    time_t writeStartTime = time(NULL);

    const off64_t lastChunkOffset = (fileSize % m_nChunkSize == 0) ? (fileSize - m_nChunkSize) : (fileSize - (fileSize % m_nChunkSize));
    off64_t chunkOffset = 0;
    std::vector<std::future<bool>> writeChunkFutures;
    bool ret = true;

    if(fileSize > 0)
    {
        // Iterate until the penultimate chunk
        while(chunkOffset < fileSize)
        {
            std::shared_ptr<FileData> writeData = UploadCache::GetInstance().Get(cacheFilePath, chunkOffset);
            if(writeData == nullptr)
            {
                LOG_ERROR("Upload cache chunk not found for remotePath: {}, chunkOffset: {}", remotePath, chunkOffset);
                ret = false;
                break;
            }

            if (chunkOffset == lastChunkOffset)
            {
                // Now the last chunk could have fewer bytes than the chunk size, we need to update the buffersize accordingly
                writeData->BufferSize = fileSize - lastChunkOffset;
            }

            // Make sure we don't exceed the max allowed concurrency
            PreChunkOpCheck(false /* isLowPriority */);

            // Upload the chunk to the cache server
            writeChunkFutures.push_back(std::async(
                std::launch::async,
                &StreamingClient::WriteChunkToCacheImpl,
                this,
                std::ref(cacheFilePath),
                chunkOffset,
                writeData,
                version));

            chunkOffset += writeData->BufferSize;
        }

        // Make sure the uploads have completed
        for (auto& writeChunkFuture: writeChunkFutures)
        {
            if (!writeChunkFuture.get())
            {
                LOG_ERROR("Failed to write chunks to cache, path={}", remotePath);
                ret = false;
            }
        }
    }

    if (ret)
    {
        m_nCacheBytesWritten += fileSize;

        time_t now = time(NULL);
        uint64_t timeDiffSecs = now - writeStartTime;
        if (timeDiffSecs > 0)
        {
            double cacheWriteThroughputMbps = ((double)fileSize * 8) / (timeDiffSecs * 1024 * 1024);
            LOG_INFO("Cache write throughput for mount={} and file={} is {} Mbps."
                " There may be other files uploading in parallel.",
                m_strMountName,
                remotePath,
                cacheWriteThroughputMbps);
        }
    }

    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
bool StreamingClient::WriteChunkToCacheImpl(
    const std::string& filePath,
    const off64_t chunkOffset,
    const std::shared_ptr<const FileData> fileData,
    const std::string& version)
{
    bool ret = false;
    auto cacheClient = std::atomic_load(&m_spCacheClient);
    if(cacheClient)
    {
        ret = cacheClient->Upload(
            m_strMountName,
            filePath,
            version,
            chunkOffset,
            fileData,
            true /* isWrite */);
    }

    // If remote write is not enabled or remote write is async, release the chunk as it is no longer needed
    if (!m_bEnableRemoteWrite || m_bEnableRemoteWriteAsync)
    {
        UploadCache::GetInstance().PurgeChunk(filePath, chunkOffset);
        m_nWriteChunksTotalCompleted++;
    }

    m_nWriteChunksRemoteCache++;
    PostChunkOpComplete();
    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
bool StreamingClient::WriteChunksToRemoteStore(
    const std::string& remotePath,
    const off64_t fileSize,
    const std::string& version)
{
    if (!m_bEnableRemoteWrite)
    {
        LOG_DEBUG("Remote write not enabled, skipping write to remote store for path={}", remotePath);
        return true; // No-op
    }

    if(m_bEnableRemoteWriteAsync)
    {
        // Capture version now — the map entry will be erased after Close() returns,
        // but the async worker needs it later for CommitBlocksForUpload and cache reads.
        std::unique_lock<std::mutex> lock(m_mutexQueue);
        m_uploadQueue.push(RemoteUploadContext(remotePath, fileSize, version));
        lock.unlock();
        m_cvQueue.notify_one();
    }
    else
    {
        return WriteChunksToRemoteStoreImpl(
            remotePath,
            fileSize,
            false /* copyFromCacheToRemote */,
            version);
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool StreamingClient::WriteChunksToRemoteStoreImpl(
    const std::string& remotePath,
    const off64_t fileSize,
    bool copyFromCacheToRemote,
    const std::string& version)
{
    // Upload any chunks to the remote store
    time_t writeStartTime = time(NULL);
    const std::string cacheFilePath = GetCacheFilePath(remotePath);
    const off64_t lastChunkOffset = (fileSize % m_nChunkSize == 0) ? (fileSize - m_nChunkSize) : (fileSize - (fileSize % m_nChunkSize));
    off64_t chunkOffset = 0;
    std::vector<std::future<bool>> writeChunkFutures;
    bool ret = true;

    if(fileSize > 0)
    {
        // This is not a chunk operation so no need to check for concurrency limits
        size_t numBlocks = (fileSize + m_nChunkSize - 1) / m_nChunkSize;
        std::atomic_load(&m_spRemoteClient)->PrepareBlocksForUpload(remotePath, numBlocks);

        // Iterate through chunks
        while(chunkOffset < fileSize)
        {
            size_t chunkSize = (chunkOffset == lastChunkOffset) ? (fileSize - lastChunkOffset) : m_nChunkSize;
            if (copyFromCacheToRemote)
            {
                writeChunkFutures.push_back(
                    WriteChunkToRemoteStoreFromCacheAsync(
                        remotePath,
                        chunkOffset,
                        chunkSize,
                        version));
            }
            else
            {
                writeChunkFutures.push_back(
                    WriteChunkToRemoteStoreFromUploadCacheAsync(
                        remotePath,
                        chunkOffset,
                        chunkSize));
            }

            chunkOffset += chunkSize;
        }

        // Make sure the uploads have completed
        for (auto& writeChunkFuture: writeChunkFutures)
        {
            if (!writeChunkFuture.get())
            {
                LOG_ERROR("Failed to stage chunks to remote, path={}", remotePath);
                ret = false;
            }
            else
            {
                if (copyFromCacheToRemote) m_nWriteChunksRemoteStoreAsync++;
                else m_nWriteChunksRemoteStoreSync++;
            }
        }
    }

    if(ret)
    {
        // This is not a chunk operation so no need to check for concurrency limits
        int result = std::atomic_load(&m_spRemoteClient)->CommitBlocksForUpload(remotePath, version);
        if(result < 0)
        {
            LOG_ERROR("Failed to commit blocks for upload to remote store, path={}", remotePath);
            ret = false;
        }
    }

    if (ret)
    {
        m_nRemoteBytesWritten += fileSize;
        time_t now = time(NULL);
        uint64_t timeDiffSecs = now - writeStartTime;

        if (timeDiffSecs > 0)
        {
            double remoteWriteThroughputMbps = ((double)fileSize * 8) / (timeDiffSecs * 1024 * 1024);
            LOG_INFO("Remote write throughput for mount={} and file={} is {} Mbps."
                " There may be other files uploading in parallel.",
                m_strMountName,
                remotePath,
                remoteWriteThroughputMbps);
        }
    }

    return ret;
}
//---------------------------------------------------------------------------------------------------------------------
std::future<bool> StreamingClient::WriteChunkToRemoteStoreFromUploadCacheAsync(
    const std::string& remotePath,
    off64_t chunkOffset,
    size_t chunkSize /* this will be chunk size, except for last chunk */)
{
    // This function gets called only for sync uploads, so not low priority
    // Note: StageChunk() will call PostChunkOpComplete()
    PreChunkOpCheck(false /* isLowPriority */);
    std::shared_ptr<FileData> writeData = UploadCache::GetInstance().Get(
        GetCacheFilePath(remotePath),
        chunkOffset);
    if (writeData == nullptr)
    {
        LOG_ERROR("Upload cache chunk not found for remotePath: {}, chunkOffset: {}", remotePath, chunkOffset);
        throw RemoteClientException("InternalError: Upload cache chunk not found");
    }

    writeData->BufferSize = chunkSize;
    return std::async(
        std::launch::async,
        &StreamingClient::StageChunk,
        this,
        std::ref(remotePath),
        chunkOffset,
        writeData);
}
//---------------------------------------------------------------------------------------------------------------------
std::future<bool> StreamingClient::WriteChunkToRemoteStoreFromCacheAsync(
    const std::string& remotePath,
    off64_t chunkOffset,
    size_t chunkSize /* this will be chunk size, except for last chunk */,
    const std::string& version)
{
    // This function gets called only for async uploads, which can be low priority
    // Note: StageChunk() will call PostChunkOpComplete()
    PreChunkOpCheck(true /* isLowPriority */);

    const std::string cacheFilePath = GetCacheFilePath(remotePath);
    auto cacheClient = std::atomic_load(&m_spCacheClient);
    if (!cacheClient)
    {
        LOG_ERROR("Cache client not configured, cannot read chunk from remote cache for path={}", remotePath);
        throw RemoteClientException("InternalError: Cache client not configured");
    }

    // Read the chunk from remote cache
    std::shared_ptr<FileData> readChunkData =
        std::make_shared<FileData>(
            chunkOffset,
            chunkSize,
            nullptr,
            false /* setToZeros */);

    uint64_t readLength = 0;
    CacheStatus cacheStatus = cacheClient->DownloadSync(
        m_strMountName,
        cacheFilePath,
        version,
        chunkOffset,
        0,
        chunkSize,
        readChunkData->Buffer.get(),
        readLength);
    if (cacheStatus != CacheStatus::SUCCESS)
    {
        LOG_ERROR("Failed to read chunk from remote cache for path={}, chunkOffset={}", remotePath, chunkOffset);
        throw RemoteClientException("InternalError: Failed to read chunk from remote cache");
    }

    readChunkData->BufferSize = readLength;

    return std::async(
        std::launch::async,
        &StreamingClient::StageChunk,
        this,
        std::ref(remotePath),
        chunkOffset,
        readChunkData);
}
//---------------------------------------------------------------------------------------------------------------------
bool StreamingClient::StageChunk(const std::string& remotePath, const off64_t chunkOffset, std::shared_ptr<FileData> fileData)
{
    // Assuming chunkOffset is always a multiple of m_nChunkSize
    off64_t blockId = chunkOffset / m_nChunkSize;
    int ret = std::atomic_load(&m_spRemoteClient)->StageBlocksForUpload(
        remotePath,
        fileData->Buffer.get(),
        fileData->BufferSize,
        blockId);
    
    if (ret != 0)
    {
        LOG_ERROR("Failed to stage chunk to remote store, path={} chunkOffset={} blockId={}",
            remotePath,
            chunkOffset,
            blockId);
        throw RemoteClientException("RemoteError: Failed to stage chunk to remote store");
    }

    // We dont need the chunk any longer as its commited to blob, purge the chunk from the upload cache to free up memory
    UploadCache::GetInstance().PurgeChunk(remotePath, chunkOffset);

    m_nWriteChunksTotalCompleted++;
    PostChunkOpComplete();
    return !ret;
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::WriteRemoteAsyncUploadWorker()
{
    LOG_INFO("Started write remote upload worker");
    while(!m_stopRemoteAsyncUpload)
    {
        std::unique_lock<std::mutex> queueLock(m_mutexQueue);
        m_cvQueue.wait(queueLock,
            [this]
            {
                return !this->m_uploadQueue.empty() || this->m_stopRemoteAsyncUpload;
            });

        if(m_stopRemoteAsyncUpload)
        {
            break;
        }

        if(m_uploadQueue.empty())
        {
            continue;
        }

        RemoteUploadContext remoteUploadContext = std::move(m_uploadQueue.front());
        m_uploadQueue.pop();
        queueLock.unlock();

        if(!WriteChunksToRemoteStoreImpl(
            remoteUploadContext.m_strRemotePath,
            remoteUploadContext.m_fileSize,
            true /* copyFromCacheToRemote */,
            remoteUploadContext.m_version))
        {
            LOG_ERROR("Failed to write chunks to remote store, path={}", remoteUploadContext.m_strRemotePath);
        }
    }

    LOG_INFO("Shutdown WriteRemoteAsyncUploadWorker");
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
    // Log stats only if the read chunks count has changed
    if (m_nReadChunksTotal > m_nLastLoggedReadChunksTotal)
    {
        LogReadChunkStats();
        LogGetPropertiesStats();
        LogDownloadLatencyStats();
        m_nLastLoggedReadChunksTotal.store(m_nReadChunksTotal);
    }

    // Log write stats only if the write chunks count has changed
    if (m_nLastLoggedWriteChunksTotal < m_nWriteChunksTotalCompleted)
    {
        LOG_INFO("WriteChunk stats: MountName={} Started={} Completed={}"
            " RemoteCache={} RemoteStoreSync={} RemoteStoreAsync={} ZeroCopy={}",
            m_strMountName,
            m_nWriteChunksTotalStarted,
            m_nWriteChunksTotalCompleted,
            m_nWriteChunksRemoteCache,
            m_nWriteChunksRemoteStoreSync,
            m_nWriteChunksRemoteStoreAsync,
            m_nWriteChunksZeroCopy);

        m_nLastLoggedWriteChunksTotal.store(m_nWriteChunksTotalCompleted);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void StreamingClient::LogReadChunkStats()
{
    LOG_INFO("ReadChunk stats: MountName={} ChunkSize={} Total={} PrefetchCache={} RemoteCache={} RemoteClient={}"
        " ZeroCopy={} SubChunk={}",
        m_strMountName,
        m_nChunkSize,
        m_nReadChunksTotal,
        m_nReadChunksPrefetchCache,
        m_nReadChunksRemoteCache,
        m_nReadChunksRemoteClient,
        m_nReadChunksZeroCopy,
        m_nReadChunksSubChunk);

    LOG_INFO("ReadFile requested stats: MountName={} TotalReads={} 128k={} 4MB={}, 100MB={} 1GB={} >1GB={}"
        " TotalBytes={} MaxReadBytes={}",
        m_strMountName,
        m_nReadFileTotal[0],
        m_nRead128K[0],
        m_nRead4MB[0],
        m_nRead100MB[0],
        m_nRead1GB[0],
        m_nReadLarge[0],
        m_nReadTotalBytes[0],
        m_nReadMax[0]);

    LOG_INFO("ReadFile serviced stats: MountName={} TotalReads={} 128k={} 4MB={}, 100MB={} 1GB={} >1GB={}"
        " TotalBytes={} MaxReadBytes={}",
        m_strMountName,
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
