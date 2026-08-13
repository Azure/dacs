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

#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>
#include <array>
#include <queue>
#include <thread>
#include <future>
#include <optional>
#include "IRemoteClient.h"
#include "FileData.h"
#include "ICacheClient.h"
#include "IAttributesCache.h"
//---------------------------------------------------------------------------------------------------------------------
class StreamingClient
{
    public:        
        StreamingClient(
            /*[in] */ const std::string& mountName,
            /*[in] */ std::shared_ptr<ICacheClient> cacheClient,
            /*[in] */ std::shared_ptr<IAttributesCache> attributesCache);

        ~StreamingClient();

        void OpenFile(/*[in] */ const std::string& remotePath);

        ssize_t ReadFile(
            /*[in] */ const std::string& remotePath,
            /*[out]*/ void *buf,
            /*[in] */ size_t readSize,            
            /*[in] */ off64_t readOffset,
            /*[in] */ off64_t fileSize,
            /*[in] */ const std::string& version);

        ssize_t WriteFile(
            /*[in] */ const std::string& remotePath,
            /*[in] */ const void *buf,
            /*[in] */ size_t writeSize,
            /*[in] */ off64_t writeOffset);

        std::string ReadDirectory(
            /*[in] */ const std::string& remotePath,
            /*[in] */ const std::string& continuationToken,
            /*[out]*/ std::vector<DirectoryEntry>& dirEntries);

        int GetProperties(
            /*[in] */ const std::string& remotePath,
            /*[out]*/ FileAttributes& attributes);

        void Close(
            /*[in] */ const std::string& remotePath,
            /*[in]*/ const std::string& existingVersion,
            /*[in]*/ const bool isWriteMode);

        // APIs to handle shared state when a process is forked (invoked by ContentHandler)
        // Processes which are forked share the system state including memory and open file descriptors.
        // We need to ensure network connections (sockets) are not shared across multiple processes.
        void ForkHandlerPrepare();
        void ForkHandlerParent(
            /*[in] */ std::shared_ptr<ICacheClient> cacheClient,
            /*[in] */ std::shared_ptr<IAttributesCache> attributesCache);
        void ForkHandlerChild(
            /*[in] */ std::shared_ptr<ICacheClient> cacheClient,
            /*[in] */ std::shared_ptr<IAttributesCache> attributesCache);

        /*
         * APIs to reset\clear clients on a process fork
         */
        void SetClients(
            /*[in] */ std::shared_ptr<ICacheClient> cacheClient,
            /*[in] */ std::shared_ptr<IAttributesCache> attributesCache);
        void ClearClients();

        #ifdef STORAGE_INTERCEPT_UT
        // APIs to get stats for read chunk counters, used in unit testing
        uint64_t GetReadChunksZeroCopy() const { return m_nReadChunksZeroCopy; }
        uint64_t GetReadChunksSubChunk() const { return m_nReadChunksSubChunk; }
        uint64_t GetReadChunksPrefetchCache() const { return m_nReadChunksPrefetchCache; }
        #endif

    private:
        struct RemoteUploadContext
        {
            RemoteUploadContext(
                const std::string& remotePath,
                const off64_t fileSize,
                const std::string& version)
                : m_strRemotePath(remotePath)
                , m_fileSize(fileSize)
                , m_version(version)
            {
            }

            const std::string                   m_strRemotePath;
            const off64_t                       m_fileSize;
            const std::string                   m_version;
        };

        const std::string                  m_strMountName;
        const std::string                  m_strRemotePathPrefix;

        // Log every read on completion
        const bool                         m_enableReadIOLogs;

        // Avoid remote downloads when cache lock is not held
        const bool                         m_bRemoteDownloadOnlyWhenLockHeldEnabled;
        const int                          m_nRemoteDownloadOnlyWhenLockHeldMaxAttempts;
        const uint64_t                     m_nRemoteDownloadOnlyWhenLockHeldPollingIntervalInSecs;

        // The following shared_ptrs are updated/reset on a process fork,
        // so ensure all access to this is synchronized via atomic APIs 
        std::shared_ptr<IRemoteClient>     m_spRemoteClient;
        std::shared_ptr<ICacheClient>      m_spCacheClient;
        std::shared_ptr<IAttributesCache>  m_spAttributesCache;

        const uint32_t                     m_nChunkSize;

        // Sub-chunk read optimization: bypass full-chunk fetch for small cache-hit reads
        const bool                         m_bEncryptionEnabled;
        const bool                         m_bChecksumValidationEnabled;

        // Parallel chunk download related
        const bool                         m_bEnableChunkOpParallelism;
        const uint32_t                     m_nMaxChunkOpConcurrency;
        std::atomic_uint64_t               m_nHighPriorityChunkOpsPending;
        std::atomic_uint64_t               m_nChunkOpsInProgress;
        std::mutex                         m_mutexChunkOps;
        std::condition_variable            m_cvChunkOps;

        // Prefetch cache related
        const bool                         m_bEnableLargePrefetch;
        size_t                             m_nPrefetchWindowSize;

        // Write support related
        const bool                         m_bEnableWrite;
        const bool                         m_bEnableRemoteWrite;
        const bool                         m_bEnableRemoteWriteAsync;
        std::atomic_bool                   m_stopRemoteAsyncUpload;
        std::queue<RemoteUploadContext>    m_uploadQueue;
        std::mutex                         m_mutexQueue;
        std::condition_variable            m_cvQueue;
        std::vector<std::unique_ptr<std::thread>> m_remoteAsyncUploadThreads;

        // Stats for cache and remote writes
        std::atomic_uint64_t               m_nCacheBytesWritten;
        std::atomic_uint64_t               m_nRemoteBytesWritten;

        // Stats for chunk cache and attributes cache
        std::atomic_uint64_t               m_nReadChunksTotal;
        std::atomic_uint64_t               m_nReadChunksPrefetchCache;
        std::atomic_uint64_t               m_nReadChunksRemoteCache;
        std::atomic_uint64_t               m_nReadChunksRemoteClient;
        std::atomic_uint64_t               m_nReadChunksZeroCopy;
        std::atomic_uint64_t               m_nReadChunksSubChunk;
        std::atomic_uint64_t               m_nWriteChunksTotalStarted;
        std::atomic_uint64_t               m_nWriteChunksTotalCompleted;
        std::atomic_uint64_t               m_nWriteChunksRemoteCache;
        std::atomic_uint64_t               m_nWriteChunksRemoteStoreSync;
        std::atomic_uint64_t               m_nWriteChunksRemoteStoreAsync;
        std::atomic_uint64_t               m_nWriteChunksZeroCopy;
        std::atomic_uint64_t               m_nGetPropertiesTotal;
        std::atomic_uint64_t               m_nGetPropertiesCache;
        std::atomic_uint64_t               m_nGetPropertiesRemoteClient;
        std::atomic_uint64_t               m_nLastLoggedReadChunksTotal;
        std::atomic_uint64_t               m_nLastLoggedWriteChunksTotal;

        // Stats for download latencies
        static constexpr unsigned          m_nLatencySamplesVecSize = 1000;
        std::vector<unsigned>              m_vecCacheDownloadLatencies;
        std::vector<unsigned>              m_vecRemoteDownloadLatencies;
        std::atomic<uint64_t>              m_nTotalCacheDownloadLatencySamples;
        std::atomic<uint64_t>              m_nTotaRemotelDownloadLatencySamples;

        // Stats for application requested/serviced reads
        // Index 0 is for application requested reads and 1 is for final serviced reads
        std::array<std::atomic_uint64_t, 2> m_nReadFileTotal;
        std::array<std::atomic_uint64_t, 2> m_nReadTotalBytes;
        std::array<std::atomic_uint64_t, 2> m_nRead128K;
        std::array<std::atomic_uint64_t, 2> m_nRead4MB;
        std::array<std::atomic_uint64_t, 2> m_nRead100MB;
        std::array<std::atomic_uint64_t, 2> m_nRead1GB;
        std::array<std::atomic_uint64_t, 2> m_nReadLarge;
        std::array<std::atomic_uint64_t, 2> m_nReadMax;

        // Long running thread for logging.
        bool m_bStopLogging;
        std::thread m_loggingThread;
        std::condition_variable m_loggingCV;
        std::mutex m_loggingMutex;

        // Switch to enable zero-copy read
        const bool                         m_bEnableZeroCopy;

        // Switch to enable versioned cache keys
        const bool                         m_bEnableVersionedCacheKeys;

        // Switch to enable consistent mode
        const bool                         m_bEnableConsistentMode;

        // Switch to enable garbage collection (eviction of stale cache chunks) in consistent mode
        const bool                         m_bEnableGarbageCollectionInConsistentMode;

        // Wrapper to call ReadChunkImpl() and update the counters tracking read chunk concurrency
        std::shared_ptr<const FileData> ReadChunk(
            /*[in] */ const std::string& remotePath,
            /*[in] */ off64_t chunkOffset,
            /*[in] */ void* pUserBuf,
            /*[in] */ const std::string& version);

        /*
         * Helper to read chunk at given offset.
         *
         * It prefers to read from prefetch cache first, then the remote cache and then from remote storage.
         * Note: Do NOT call this function directly but via ReadChunk(), so that read chunk concurrency can be
         *       properly controlled.
         */
        std::shared_ptr<const FileData> ReadChunkImpl(
            /*[in] */ const std::string& remotePath,
            /*[in] */ off64_t chunkOffset,
            /*[in] */ void* pUserPassedbuffer,
            /*[in] */ const std::string& version);

        // Helper function to read chunk and copy the relevant part to user provided buffer.
        // Returns the number of bytes copied into buffer, -1 in case of error.
        ssize_t ReadChunkAndCopyToBuffer(
            /*[in] */ const std::string& remotePath,
            /*[in] */ off64_t chunkOffset,
            /*[in] */ void* pUserPassedbuffer,
            /*[in] */ size_t bufSize,
            /*[in] */ size_t chunkCopyOffset,
            /*[in] */ const std::string& version);

        // Attempt sub-chunk fast path for small reads. Returns bytes copied on success,
        // or std::nullopt if caller should fall through to the full-chunk path.
        std::optional<ssize_t> TrySubChunkRead(
            /*[in] */ const std::string& remotePath,
            /*[in] */ off64_t chunkOffset,
            /*[in] */ void* pUserBuf,
            /*[in] */ size_t bufSize,
            /*[in] */ size_t chunkCopyOffset,
            /*[in] */ const std::string& version);

        // Probe the cache server with a retry loop. Returns the final CacheStatus.
        // On SUCCESS, readLength is set and metrics are recorded.
        // Handles NOTFOUND retry (when lockHeldEnabled), INTERNAL_ERROR, CHECKSUM_MISMATCH.
        cacheutils::CacheStatus ProbeCacheServerWithRetry(
            /*[in] */ const std::string& cacheFilePath,
            /*[in] */ off64_t chunkOffset,
            /*[in] */ uint64_t startOffset,
            /*[in] */ uint64_t length,
            /*[in] */ char* buffer,
            /*[out]*/ uint64_t& readLength,
            /*[in] */ const std::string& version);

        // Download a full chunk from blob storage. Returns bytes read, or -1 on error.
        ssize_t DownloadFullChunkFromBlob(
            /*[in] */ const std::string& remotePath,
            /*[in] */ off64_t chunkOffset,
            /*[in] */ FileData& readData);

        // Function to serve read by sequentially downloading the required chunks.
        ssize_t ReadChunksSequential(
            /*[in] */ const std::string& remotePath,
            /*[in] */ void *pUserPassedbuffer,
            /*[in] */ size_t totalReadSize,
            /*[in] */ off64_t readOffset,
            /*[in] */ const std::string& version);

        // Function to serve read by parallely downloading the required chunks.
        ssize_t ReadChunksParallel(
            /*[in] */ const std::string& remotePath,
            /*[in] */ void *pUserPassedbuffer,
            /*[in] */ size_t totalReadSize,
            /*[in] */ off64_t readOffset,
            /*[in] */ const std::string& version);

        // Function to ensure all chunks in prefetch window(s) overlapping with current read are populated in the
        // prefetch cache. This is called only when large prefetch optimization is enabled.
        void EnsurePrefetchCacheWindowPopulated(
            /*[in] */ const std::string& remotePath,
            /*[in] */ size_t totalReadSize,
            /*[in] */ off64_t readOffset,
            /*[in] */ off64_t fileSize,
            /*[in] */ const std::string& version);

        // Function to be called before starting any chunk operation.
        // It increments the counter for concurrent read chunks and waits till the concurrency is below the allowed
        // max concurrency limit.
        void PreChunkOpCheck(/*[in] */ bool isLowPriority);

        // Function to be called after completing any chunk operation.
        void PostChunkOpComplete();

        // Helper functions to flush upload cache to cache server
        bool WriteChunksToCache(/*[in]*/ const std::string& remotePath, /*[in]*/ const FileAttributes& attributes);
        bool WriteChunkToCacheImpl(
            /*[in] */ const std::string& filePath,
            /*[in] */ const off64_t chunkOffset,
            /*[in] */ const std::shared_ptr<const FileData> fileData,
            /*[in] */ const std::string& version);

        // Helper functions to flush upload cache to remote store
        bool WriteChunksToRemoteStore(
            /*[in]*/ const std::string& remotePath,
            /*[in]*/ const off64_t fileSize,
            /*[in]*/ const std::string& version);
        bool WriteChunksToRemoteStoreImpl(
            /*[in]*/ const std::string& remotePath,
            /*[in]*/ const off64_t fileSize,
            /*[in]*/ bool copyFromCacheToRemote,
            /*[in]*/ const std::string& version);
        std::future<bool> WriteChunkToRemoteStoreFromCacheAsync(
            /*[in] */ const std::string& remotePath,
            /*[in] */ const off64_t chunkOffset,
            /*[in] */ const size_t chunkSize,
            /*[in] */ const std::string& version);
        std::future<bool> WriteChunkToRemoteStoreFromUploadCacheAsync(
            /*[in] */ const std::string& remotePath,
            /*[in] */ const off64_t chunkOffset,
            /*[in] */ const size_t chunkSize);
        // Upload an individual data chunk to remote store
        bool StageChunk(
            /*[in] */ const std::string& remotePath,
            /*[in] */ const off64_t chunkOffset,
            /*[in] */ std::shared_ptr<FileData> fileData);
        void WriteRemoteAsyncUploadWorker();

        // Evict cached chunks for a specific version of a file
        void EvictCacheForFileVersion(
            /*[in]*/ const std::string& remotePath,
            /*[in]*/ const std::string& version);

        // Helper funtions to log stats
        void LongRunningLogStats(const uint32_t);
        void LogStats();

        // Helper funtion to record read stats 
        void UpdateReadStats(
            /*[in] */ size_t readSize,
            /*[in] */ bool finalRead);

        // Check and log the read IO
        void LogReadIO(
            /*[in] */ const std::string& remotePath,
            /*[in] */ off64_t readOffset,
            /*[in] */ size_t requestedSize,
            /*[in] */ ssize_t actualSize);

        void LogReadChunkStats();
        void LogGetPropertiesStats();
        void LogDownloadLatencyStats();
        void LogDownloadLatencyStatsInner(const std::vector<unsigned>& vecLatencies,
                                          unsigned nNumSamples,
                                          const char* strSource);

        // Given a relative remote path, construct a unique locally addressable path
        std::string GetCacheFilePath(/*[in] */ const std::string& path) const;
};
//---------------------------------------------------------------------------------------------------------------------
