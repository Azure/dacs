///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <memory>
#include <sys/types.h>
//---------------------------------------------------------------------------------------------------------------------
/*
 * Represents a portion of data read from a file.
 */
struct FileReadData
{
    FileReadData(off64_t offset, size_t bufSize)
        : Offset(offset)
        , Buffer(new char[bufSize])
        , BufferSize(bufSize)
    {}

    // The offset at which data was read from.
    const off64_t       Offset;

    // The data bytes read.
    std::unique_ptr<char[]>     Buffer;
    size_t                      BufferSize;
};
//---------------------------------------------------------------------------------------------------------------------
