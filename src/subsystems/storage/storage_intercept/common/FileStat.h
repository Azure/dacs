///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <stdint.h>
//---------------------------------------------------------------------------------------------------------------------
// Architecture-agnostic file stat struct for the Python binding API.
// Uses fixed-width integer types so the memory layout is identical on all
// architectures (x86_64, aarch64, etc.), unlike the system's struct stat64
// whose field sizes and padding vary by platform.
//
// Only includes fields that are actually populated by the storage intercept layer.
// Time fields use "_sec" suffix to avoid collision with st_atime/st_mtime/st_ctime
// macros defined in <sys/stat.h>.
//---------------------------------------------------------------------------------------------------------------------
struct FileStat
{
    uint32_t st_mode;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_atime_sec;
    int64_t  st_mtime_sec;
    int64_t  st_ctime_sec;
};
//---------------------------------------------------------------------------------------------------------------------
