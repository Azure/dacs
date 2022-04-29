///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "SocketUtils.h"
#include "Constants.h"
#include "Logger.h"
#include <chrono>
#include <iostream>
#include <unistd.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
uint64_t ConvertBytesToInt(char *data, int size)
{
    uint64_t result = 0;
    uint64_t mask = 255;
    for (int i = 0; i < size; ++i)
    {
        uint64_t d = (uint64_t)data[i] & mask;
        uint64_t x = d << (8 * (size - (i + 1)));
        result |= (x);
    }
    return result;
}
//---------------------------------------------------------------------------------------------------------------------
void ConvertIntToBytes(uint64_t source, char* destination, int index, int size)
{
    uint64_t allOnes = 255;
    for (int i = 0; i < size; ++i)
    {
        int numBitsToShift = 8 * (size - (i + 1));
        uint64_t mask = source >> numBitsToShift;
        destination[index + i] = (char)(allOnes & mask);
    }
}
//---------------------------------------------------------------------------------------------------------------------
int ReadBytesToArray(int clientFd, char* data, int size)
{
    int totalRead = 0;
    int tempRead;

    while (totalRead < size)
    {
        if ((tempRead = read(clientFd, data + totalRead, size - totalRead)) > 0)
        {
            totalRead += tempRead;
        }
        else 
        {
            LOG_WARN("read client incomplete, fd: {}, errno: {}:{}, expected: {}, read: {}",
                clientFd, errno, strerror(errno), size, totalRead);
            return totalRead;
        }
    }
    return totalRead;
}
//---------------------------------------------------------------------------------------------------------------------
bool SendMessageToRemote(int clientFd, const ::google::protobuf::Message& message)
{
    string protoBuffer;
    if (!message.SerializeToString(&protoBuffer))
    {
        LOG_WARN("couldn't serialize message proto");
        return false;
    }

    string outputBuffer(REQUEST_METADATA_SIZE, 0);
    ConvertIntToBytes(protoBuffer.size(), outputBuffer.data(), 0, REQUEST_METADATA_SIZE);
    
    outputBuffer += protoBuffer;

    if (write(clientFd, outputBuffer.data(), outputBuffer.size()) != outputBuffer.size())
    {
        LOG_WARN("couldn't write response to client fd: {}, errno: {}:{}", clientFd, errno, strerror(errno));
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------