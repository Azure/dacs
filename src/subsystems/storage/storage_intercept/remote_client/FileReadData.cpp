///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "FileReadData.h"
#include "StorageInterceptLogger.h"
//---------------------------------------------------------------------------------------------------------------------
FileReadData::FileReadData(off64_t offset, size_t bufSize)
    : Offset(offset)
    , Buffer((char *)aligned_alloc(BufferAlignmentSize, bufSize), FreeAlignedBuffer)
    , BufferSize(bufSize)
{
    if (!Buffer)
    {
        LOG_CRITICAL("Failed to create aligned buffer. alignment={} bufSize={}",
            BufferAlignmentSize, bufSize);
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