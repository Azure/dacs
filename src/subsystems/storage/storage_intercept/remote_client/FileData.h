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
#include <functional>
#include <sys/types.h>
#include <stdlib.h>
//---------------------------------------------------------------------------------------------------------------------
/*
 * Represents a portion of data read from a file.
 */
using unique_aligned_buffer = std::unique_ptr<char, std::function<void(char*)>>;
class FileData
{
    public:
    FileData(off64_t offset, size_t bufSize, char* pUserBuf, bool setToZeros);
    ~FileData() = default;

    // The offset at which data was read from or written into
    const off64_t       Offset;

    // The data bytes read or written.
    unique_aligned_buffer       Buffer;
    size_t                      BufferSize;
    const bool                  bIsUserBuffer;

    // Set alignment size to filesystem's logical block size to enable IO optimizations
    static constexpr size_t     BufferAlignmentSize = 512;

    static void FreeAlignedBuffer(char* buffer);
};
//---------------------------------------------------------------------------------------------------------------------