//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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
    bool AddAuthParams(cache::Request& request, const std::string& strCacheAccountName, const std::string& strCacheAccountKey);
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