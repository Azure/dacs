///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <set>
#include <shared_mutex>
#include "FileData.h"
//---------------------------------------------------------------------------------------------------------------------
class UploadCache
{
    /* UploadCache manages the in-progress write file chunks which could be sent for upload to the cache to blob.
     *
     * For each file, it maintains
     * - A set of contiguous fixed-sizechunks in increasing offset order.
     * - The size of the file post last write (needed as the last chunk may be partial).
     */
    public:
        /* Singleton */
        static UploadCache& GetInstance();
    
    public:
        // Adds/Updates the upload cache with the given chunk data for the specified file path and offset.
        // Expects chunk offsets to be added in increasing order and always be of chunk size.
        //
        // If there are any holes between the last cached chunk and the new chunk offset, zeroed out chunks
        // are created to fill the holes.
        void Put(
            /*[in] */ const std::string& path,
            /*[in] */ std::shared_ptr<FileData> uploadData,
            /*[in] */ size_t newFileSize);
        size_t GetFileSize(
            /*[in] */ const std::string& path) const;
        std::shared_ptr<FileData> Get(
            /*[in] */ const std::string& path,
            /*[in] */ off64_t offset) const;
        // Updates the tracked file size if newFileSize is larger than the current tracked size.
        // Called after writes to existing chunks that extend the file beyond the previously tracked size.
        void UpdateFileSize(
            /*[in] */ const std::string& path,
            /*[in] */ size_t newFileSize);
        void Purge(/*[in] */ const std::string& path);
        void PurgeChunk(
            /*[in] */ const std::string& path,
            /*[in] */ off64_t offset);
    private:
        UploadCache();

        static std::string GetCacheKey(
            /*[in]*/  const std::string& path,
            /*[in]*/  off64_t offset);
    
    private:
        const size_t                                                m_nChunkSize;

        std::map<std::string, std::shared_ptr<FileData>>            m_mapUploadBlocks;
        std::map<std::string, size_t>                               m_mapFileSizes;
        mutable std::shared_mutex                                   m_mutexBlockBuffers;
};
//---------------------------------------------------------------------------------------------------------------------