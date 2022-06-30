///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>
#include "IRemoteClient.h"
#include "FileReadData.h"
#include "ICacheClient.h"
#include "IAttributesCache.h"
//---------------------------------------------------------------------------------------------------------------------
class StreamingClient
{
    public:
        static StreamingClient& GetInstance();

        void OpenFile(/*[in] */ const std::string& remotePath);

        ssize_t ReadFile(
            /*[in] */ const std::string& remotePath,
            /*[out]*/ void *buf,
            /*[in] */ size_t readSize,
            /*[in] */ off64_t readOffset);

        std::string ReadDirectory(
            /*[in] */ const std::string& remotePath,
            /*[in] */ const std::string& continuationToken,
            /*[out]*/ std::vector<DirectoryEntry>& dirEntries);

        int GetProperties(
            /*[in] */ const std::string& remotePath,
            /*[out]*/ FileAttributes& attributes);

        void Close(/*[in] */ const std::string& remotePath);

        // APIs to handle shared state when a process is forked.
        // Processes which are forked share the system state including memory and open file descriptors.
        // We need to ensure network connections (sockets) are not shared across multiple processes.
        static void ForkHandlerPrepare();
        static void ForkHandlerParent();
        static void ForkHandlerChild();

    private:
        StreamingClient();

        ~StreamingClient();

        // The following shared_ptrs are updated/reset on a process fork,
        // so ensure all access to this is synchronized via atomic APIs 
        std::shared_ptr<IRemoteClient>     m_spRemoteClient;
        std::shared_ptr<ICacheClient>      m_spCacheClient;
        std::shared_ptr<IAttributesCache>  m_spAttributesCache;

        const uint32_t                     m_nChunkSize;

        // Parallel chunk download related
        const bool                         m_bEnableReadChunkParallelism;
        const uint32_t                     m_nMaxReadChunkConcurrency;
        std::atomic_uint64_t               m_nReadChunksInProgress;
        std::mutex                         m_mutexReadChunk;
        std::condition_variable            m_cvReadChunk;

        // Prefetch cache related
        const bool                         m_bEnableLargePrefetch;
        size_t                             m_nPrefetchWindowSize;

        // Stats for chunk cache and attributes cache
        std::atomic_uint64_t               m_nReadChunksTotal;
        std::atomic_uint64_t               m_nReadChunksPrefetchCache;
        std::atomic_uint64_t               m_nReadChunksRemoteCache;
        std::atomic_uint64_t               m_nReadChunksRemoteClient;
        std::atomic_uint64_t               m_nGetPropertiesTotal;
        std::atomic_uint64_t               m_nGetPropertiesCache;
        std::atomic_uint64_t               m_nGetPropertiesRemoteClient;

        /*
         * Get unique cache key.
         */
        static std::string GetCacheKey(
            /*[in] */ const std::string& remotePath,
            /*[in] */ off64_t chunkOffset);

        // Wrapper to call ReadChunkImpl() and update the counters tracking read chunk concurrency
        std::shared_ptr<const FileReadData> ReadChunk(
            /*[in] */ const std::string& remotePath,
            /*[in] */ off64_t chunkOffset);

        /*
         * Helper to read chunk at given offset.
         *
         * It prefers to read from prefetch cache first, then the remote cache and then from remote storage.
         * Note: Do NOT call this function directly but via ReadChunk(), so that read chunk concurrency can be
         *       properly controlled.
         */
        std::shared_ptr<const FileReadData> ReadChunkImpl(
            /*[in] */ const std::string& remotePath,
            /*[in] */ off64_t chunkOffset);

        // Helper function to read chunk and copy the relevant part to user provided buffer.
        // Returns the number of bytes copied into buffer, -1 in case of error.
        ssize_t ReadChunkAndCopyToBuffer(
            /*[in] */ const std::string& remotePath,
            /*[in] */ off64_t chunkOffset,
            /*[in] */ void* buf,
            /*[in] */ size_t bufSize,
            /*[in] */ size_t chunkCopyOffset);

        // Function to serve read by sequentially downloading the required chunks.
        ssize_t ReadChunksSequential(
            /*[in] */ const std::string& remotePath,
            /*[out]*/ void *buf,
            /*[in] */ size_t readSize,
            /*[in] */ off64_t readOffset);

        // Function to serve read by parallely downloading the required chunks.
        ssize_t ReadChunksParallel(
            /*[in] */ const std::string& remotePath,
            /*[out]*/ void *buf,
            /*[in] */ size_t readSize,
            /*[in] */ off64_t readOffset);

        // Function to ensure all chunks in prefetch window(s) overlapping with current read are populated in the
        // prefetch cache. This is called only when large prefetch optimization is enabled.
        void EnsurePrefetchCacheWindowPopulated(
            /*[in] */ const std::string& remotePath,
            /*[in] */ size_t readSize,
            /*[in] */ off64_t readOffset);
        
        // Function to be called before starting any read chunk (sync or async).
        // It increments the counter for concurrent read chunks and waits till the concurrency is below the allowed
        // max concurrency limit.
        void PreReadChunkCheck();

        /*
         * APIs to reset cached state, used to clear old state on a process fork
         */
        void SetClient();
        void ClearClient();

        // Helper funtion to log stats 
        void LogStats(bool forceLog = false);
};
//---------------------------------------------------------------------------------------------------------------------
