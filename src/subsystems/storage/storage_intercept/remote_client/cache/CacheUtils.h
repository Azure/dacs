//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "cache.pb.h"
#include "IConnection.h"
#include <unistd.h>
#include <string>
//---------------------------------------------------------------------------------------------------------------------
namespace cacheutils
{
    static constexpr int REQUEST_METADATA_SIZE = 4;

    bool WriteAll(IConnection* connection, void *buf, size_t nByte);
    bool WriteAllFinal(IConnection* connection, void *buf, size_t nByte);
    bool ReadAll(IConnection* connection, void *buf, size_t nByte);
    uint64_t ConvertBytesToInt(char *data, int size);
    void ConvertIntToBytes(uint64_t source, char* destination, int index, int size);
    int ReadBytesToArray(IConnection* connection, char* data, int size);
    bool SendMessageToRemote(IConnection* connection, const ::google::protobuf::Message& message);
    bool SendMessageToRemoteFinal(IConnection* connection, const ::google::protobuf::Message& message);
    bool ReadMessageFromRemote(IConnection* connection, std::string& message);
    uint32_t GetServerIndex(const std::string& filePath, uint32_t numServers);
    bool AddAuthParams(cache::Request& request);
    std::string GetCacheKey(const std::string& remotePath, off64_t chunkOffset);
}
//---------------------------------------------------------------------------------------------------------------------