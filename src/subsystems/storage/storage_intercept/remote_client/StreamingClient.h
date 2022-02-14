///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <memory>
#include "IRemoteClient.h"
#include "FileReadData.h"
#include "ICacheClient.h"
#include "IAttributesCache.h"
//---------------------------------------------------------------------------------------------------------------------
class StreamingClient
{
    public:
        static StreamingClient& GetInstance();

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

        // The following shared_ptrs are updated/reset on a process fork,
        // so ensure all access to this is synchronized via atomic APIs 
        std::shared_ptr<IRemoteClient>     m_spRemoteClient;
        std::shared_ptr<ICacheClient>      m_spCacheClient;
        std::shared_ptr<IAttributesCache>  m_spAttributesCache;

        const uint32_t                     m_nChunkSize;

        /*
         * Get unique cache key.
         */
        static std::string GetCacheKey(
            /*[in] */ const std::string& remotePath,
            /*[in] */ off64_t chunkOffset);

        /*
         * Helper to read chunk at given offset.
         *
         * It prefers to read from prefetch cache first, then the remote cache and then from remote storage.
         */
        std::shared_ptr<const FileReadData> ReadChunk(
            /*[in] */ const std::string& remotePath,
            /*[in] */ off64_t chunkOffset);

        /*
         * APIs to reset cached state, used to clear old state on a process fork
         */
        void SetClient();
        void ClearClient();
};
//---------------------------------------------------------------------------------------------------------------------
