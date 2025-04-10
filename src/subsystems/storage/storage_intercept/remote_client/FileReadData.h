///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <memory>
#include <functional>
#include <sys/types.h>
#include <stdlib.h>
//---------------------------------------------------------------------------------------------------------------------
/*
 * Represents a portion of data read from a file.
 */
using unique_aligned_buffer = std::unique_ptr<char, std::function<void(char*)>>;
class FileReadData
{
    public:
    FileReadData(off64_t offset, size_t bufSize, char* pUserBuf);
    ~FileReadData() = default;

    // The offset at which data was read from.
    const off64_t       Offset;

    // The data bytes read.
    unique_aligned_buffer       Buffer;
    size_t                      BufferSize;
    const bool                  bIsUserBuffer;

    // Set alignment size to filesytem's logical block size to enable IO optimizations
    static constexpr size_t     BufferAlignmentSize = 512;

    static void FreeAlignedBuffer(char* buffer);
};
//---------------------------------------------------------------------------------------------------------------------