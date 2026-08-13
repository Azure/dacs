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
