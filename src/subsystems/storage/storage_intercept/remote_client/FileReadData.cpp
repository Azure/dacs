///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "FileReadData.h"
#include "StorageInterceptLogger.h"
//---------------------------------------------------------------------------------------------------------------------
FileReadData::FileReadData(off64_t offset, size_t bufSize)
    : Offset(offset)
{
    // We over provision buffer to match alignment size if requested size is not aligned to BufferAlignmentSize.
    // Adding a Warn message as this should not happen in regular flow, only used in tests.
    size_t allocSize = bufSize;
    if (bufSize % BufferAlignmentSize != 0)
    {
        allocSize = bufSize + BufferAlignmentSize - (bufSize % BufferAlignmentSize);
        LOG_WARN("Client requested unaligned size. bufSize={}, allocSize={}", bufSize, allocSize);
    }

    Buffer = unique_aligned_buffer((char *)aligned_alloc(BufferAlignmentSize, allocSize), FreeAlignedBuffer);
    BufferSize = bufSize;
    if (!Buffer)
    {
        LOG_CRITICAL("Failed to create aligned buffer. alignment={} bufSize={} allocSize={}",
            BufferAlignmentSize, bufSize, allocSize);
        throw "Failed to create aligned buffer";
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