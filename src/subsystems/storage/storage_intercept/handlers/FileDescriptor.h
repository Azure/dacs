///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <memory>
#include "DirectoryStream.h"
//---------------------------------------------------------------------------------------------------------------------
/*
 * File descriptor struct to maintain information about open files and directories. Used by ContentHandler.
 *
 * Note: We don't support all kinds of open yet, so some fields would be unused.
 */
struct FileDescriptor
{
    FileDescriptor()
        : ParentDirFd(-1)
        , OpenFlags(0x0)
        , FileOffset(0)
        , DirStream(nullptr)
    {}

    // Input params
    int ParentDirFd;
    int OpenFlags;

    // Remote path of the file opened    
    std::string RemotePath;

    // offset for file read\writes
    off64_t FileOffset;

    // read directory data (for directory streams)
    DirectoryStream* DirStream;
};
//---------------------------------------------------------------------------------------------------------------------