///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "FileReadData.h"
#include "StorageInterceptLogger.h"
//---------------------------------------------------------------------------------------------------------------------
FileReadData::FileReadData(off64_t offset, size_t bufSize, char* pUserBuf)
    : Offset(offset), BufferSize(bufSize), bIsUserBuffer(pUserBuf != nullptr)
{
    if (bIsUserBuffer) {
        // User passed buffer is expected to be aligned to BufferAlignmentSize as zero copy reads
        // are only done if userBuffer covers the entire chunk, else assert and throw.
        STORAGE_ASSERT(bufSize % BufferAlignmentSize == 0,
            "User passed buffer {} with bufSize {} is not aligned to required BufferAlignmentSize={}",
            (void *)(pUserBuf), bufSize, BufferAlignmentSize);

        // We do not free the user passed buffer as it is expected to be freed by the caller.
        Buffer = unique_aligned_buffer(
            (char *)pUserBuf,
            [](char*) {} /* empty free function */);
    } else {
        // We over provision buffer to match alignment size if requested size is not aligned to BufferAlignmentSize.
        // Adding a Warn message as this should not happen in regular flow, only used in tests.
        size_t allocSize = bufSize;
        if (bufSize % BufferAlignmentSize != 0)
        {
            allocSize = bufSize + BufferAlignmentSize - (bufSize % BufferAlignmentSize);
            LOG_WARN("Client requested unaligned size. bufSize={}, allocSize={}", bufSize, allocSize);
        }

        Buffer = unique_aligned_buffer(
            (char *)aligned_alloc(BufferAlignmentSize, allocSize),
            FreeAlignedBuffer);
        STORAGE_ASSERT(Buffer,
            "Failed to create aligned buffer. alignment={} bufSize={} allocSize={}",
            BufferAlignmentSize, bufSize, allocSize);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void FileReadData::FreeAlignedBuffer(char* buffer)
{
    if (buffer)
    {
        free(buffer);
    }
}
//---------------------------------------------------------------------------------------------------------------------