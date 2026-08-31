// Copyright (c) Microsoft Corporation.
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
#include "cache.pb.h"
#include "IConnection.h"
#include <unistd.h>
#include <string>
//---------------------------------------------------------------------------------------------------------------------
namespace cacheutils
{
    enum class CacheStatus {SUCCESS, NOTFOUND, NOTFOUND_LOCKED, AUTH_FAILED, INTERNAL_ERROR, CHECKSUM_MISMATCH};

    bool WriteAll(IConnection* connection, void *buf, size_t nByte);
    bool WriteAllFinal(IConnection* connection, const void *buf, size_t nByte);
    bool ReadAll(IConnection* connection, void *buf, size_t nByte);
    uint64_t ConvertBytesToInt(char *data, int size);
    void ConvertIntToBytes(uint64_t source, char* destination, int index, int size);
    int ReadBytesToArray(IConnection* connection, char* data, int size);
    bool SendMessageToRemote(IConnection* connection, const ::google::protobuf::Message& message);
    bool SendMessageToRemoteFinal(IConnection* connection, const ::google::protobuf::Message& message);
    bool ReadMessageFromRemote(IConnection* connection, std::string& message);
    uint32_t GetServerIndex(const char* filePath, uint32_t numServers);
    bool IsValidLastAccessTime(const char* lastAccessTime);
    std::string GetSHA256Hex(const std::string& message);

    // Compute the cache groupId for a file+version pair.
    inline std::string GetGroupIdForFileVersion(const std::string& filePath, const std::string& version)
    {
        return GetSHA256Hex(filePath + ":" + version);
    }

    // Generate a globally-unique ID. Takes ~4ms for 10K invocations.
    std::string GetCorrelationId();

    static constexpr int REQUEST_METADATA_SIZE = 4;
}
//---------------------------------------------------------------------------------------------------------------------