///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "UploadCache.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptConfig.h"
#include <mutex>
//---------------------------------------------------------------------------------------------------------------------
UploadCache::UploadCache()
    : m_nChunkSize(StorageInterceptConfig::GetInstance().GetStreamingChunkSize())
{
    LOG_INFO("Upload Cache initialized");
}
//---------------------------------------------------------------------------------------------------------------------
UploadCache& UploadCache::GetInstance()
{
    static UploadCache s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
void UploadCache::Put(const std::string& path, std::shared_ptr<FileData> uploadData, size_t newFileSize)
{
    STORAGE_ASSERT(uploadData != nullptr, "UploadCache::Put called with null uploadData");
    STORAGE_ASSERT(uploadData->BufferSize == m_nChunkSize, "UploadCache::Put called with incorrect chunk size");
    STORAGE_ASSERT(uploadData->Offset <= newFileSize, "UploadCache::Put called with inconsistent file size");

    std::unique_lock<std::shared_mutex> buffersLock(m_mutexBlockBuffers);
    auto sizeIt = m_mapFileSizes.find(path);
    if(sizeIt != m_mapFileSizes.end())
    {
        // File already has some chunks stored
        // Get the last chunkoffset
        const off64_t lastChunkOffset = (sizeIt->second % m_nChunkSize == 0) ?
            (sizeIt->second - m_nChunkSize) :
            (sizeIt->second - (sizeIt->second % m_nChunkSize));
        STORAGE_ASSERT(uploadData->Offset > lastChunkOffset,
            "UploadCache::Put called non-increasing chunk offset");

        // This occurs when a pwrite is issued with a writeOffset > fileSize, a sparse file should be created
        // with zeroed out content from the old end of file to upto the desired writeOffset
        off64_t currentChunkOffset = lastChunkOffset + m_nChunkSize;
        while(currentChunkOffset < uploadData->Offset)
        {
            LOG_DEBUG("Creating new zeroed upload cache chunk. path={} chunkOffset={}",
                path,
                currentChunkOffset);
            // We need to insert zeroed out chunks up until we get to our chunk
            std::shared_ptr<FileData> zeroedData =
                std::make_shared<FileData>(
                    currentChunkOffset,
                    uploadData->BufferSize,
                    nullptr /* userBuf */,
                    true /* setToZeros */);
            m_mapUploadBlocks[GetCacheKey(path, currentChunkOffset)] = zeroedData;
            currentChunkOffset += uploadData->BufferSize;
        }
    }
    else
    {
        STORAGE_ASSERT(uploadData->Offset == 0, "UploadCache::Put called with non-zero offset for first chunk");
    }

    m_mapUploadBlocks[GetCacheKey(path, uploadData->Offset)] = uploadData;
    m_mapFileSizes[path] = newFileSize;
}
//---------------------------------------------------------------------------------------------------------------------
size_t UploadCache::GetFileSize(const std::string& path) const
{
    std::shared_lock<std::shared_mutex> buffersLock(m_mutexBlockBuffers);    
    auto sizeIt = m_mapFileSizes.find(path);
    if (sizeIt == m_mapFileSizes.end()) return 0; // Not found
    return sizeIt->second;
}
//---------------------------------------------------------------------------------------------------------------------
void UploadCache::UpdateFileSize(const std::string& path, size_t newFileSize)
{
    std::unique_lock<std::shared_mutex> buffersLock(m_mutexBlockBuffers);
    auto sizeIt = m_mapFileSizes.find(path);
    if (sizeIt != m_mapFileSizes.end() && newFileSize > sizeIt->second)
    {
        sizeIt->second = newFileSize;
    }
}
//---------------------------------------------------------------------------------------------------------------------
std::shared_ptr<FileData> UploadCache::Get(const std::string& path, off64_t offset) const
{
    std::shared_lock<std::shared_mutex> buffersLock(m_mutexBlockBuffers);    
    std::string cacheKey = GetCacheKey(path, offset);
    auto blocksIt = m_mapUploadBlocks.find(cacheKey);
    if (blocksIt == m_mapUploadBlocks.end()) return nullptr; // Not found
    return blocksIt->second;
}
//---------------------------------------------------------------------------------------------------------------------
void UploadCache::Purge(const std::string& path)
{
    std::unique_lock<std::shared_mutex> buffersLock(m_mutexBlockBuffers);
    // Get last chunk offset
    auto sizeIt = m_mapFileSizes.find(path);
    if (sizeIt == m_mapFileSizes.end()) return; /* Nothing to purge */
    const off64_t lastChunkOffset = (sizeIt->second % m_nChunkSize == 0) ?
        (sizeIt->second - m_nChunkSize) :
        (sizeIt->second - (sizeIt->second % m_nChunkSize));

    for(off64_t offset = 0; offset <= lastChunkOffset; offset += m_nChunkSize)
    {
        const std::string cacheKey = GetCacheKey(path, offset);
        m_mapUploadBlocks.erase(cacheKey);
    }
    m_mapFileSizes.erase(sizeIt);
}
//---------------------------------------------------------------------------------------------------------------------
void UploadCache::PurgeChunk(const std::string& path, off64_t offset)
{
    std::unique_lock<std::shared_mutex> buffersLock(m_mutexBlockBuffers);

    const std::string cacheKey = GetCacheKey(path, offset);
    m_mapUploadBlocks.erase(cacheKey);
}
//---------------------------------------------------------------------------------------------------------------------
std::string UploadCache::GetCacheKey(const std::string& path, off64_t offset)
{
    return path + "##" + std::to_string(offset);
}
//---------------------------------------------------------------------------------------------------------------------