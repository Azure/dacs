//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "cache.pb.h"
#include <unistd.h>
#include <string>
//---------------------------------------------------------------------------------------------------------------------
namespace cacheutils
{
    static constexpr int REQUEST_METADATA_SIZE = 4;
    static constexpr int SOCKET_TIMEOUT_MILLIS = 2000;

    ssize_t TimedWrite(int fd, const void *buf, size_t nByte);
    ssize_t TimedRead(int fd, void *buf, size_t nByte);
    uint64_t ConvertBytesToInt(char *data, int size);
    void ConvertIntToBytes(uint64_t source, char* destination, int index, int size);
    int ReadBytesToArray(int clientFd, char* data, int size);
    bool SendMessageToRemote(int clientFd, const ::google::protobuf::Message& message);
    bool ReadMessageFromRemote(int serverFd, std::string& message);
    uint32_t GetServerIndex(const std::string& filePath, uint32_t numServers);
}
//---------------------------------------------------------------------------------------------------------------------