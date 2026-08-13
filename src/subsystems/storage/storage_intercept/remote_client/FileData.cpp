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

#include "FileData.h"
#include "StorageInterceptLogger.h"
//---------------------------------------------------------------------------------------------------------------------
FileData::FileData(off64_t offset, size_t bufSize, char* pUserBuf, bool setToZeros)
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
        
        if (setToZeros)
        {
            ::memset(Buffer.get(), 0, allocSize);
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
void FileData::FreeAlignedBuffer(char* buffer)
{
    if (buffer)
    {
        free(buffer);
    }
}
//---------------------------------------------------------------------------------------------------------------------