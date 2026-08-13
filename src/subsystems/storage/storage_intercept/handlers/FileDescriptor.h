/// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <memory>
#include <stdio.h>
#include "DirectoryStream.h"
#include "FileAttributes.h"
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
        , MountName()
        , RemotePath()
        , FileOffset(0)
        , Attributes()
        , DirStream(nullptr)
        , FileStream(nullptr)
        , IsWriteMode(false)
        , IsWriteInProgress(false)
    {}

    // Input params
    int ParentDirFd;
    int OpenFlags;

    // Mount name
    std::string MountName;

    // Remote path of the file opened    
    std::string RemotePath;

    // offset for file read\writes
    off64_t FileOffset;

    // File attributes
    FileAttributes Attributes;

    // read directory data (for directory streams)
    std::unique_ptr<DirectoryStream> DirStream;

    // File pointer
    std::unique_ptr<FILE> FileStream;

    // Write mode enabled or not
    bool IsWriteMode;

    // Is write in progress
    bool IsWriteInProgress;
};
//---------------------------------------------------------------------------------------------------------------------