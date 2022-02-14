///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheUtils.h"
#include "StorageInterceptLogger.h"
#include "StorageDlsymWrapper.h"
#include "poll.h"
#include <sys/socket.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
namespace cacheutils
{
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
    ssize_t TimedWrite(int fd, const void *buf, size_t nByte)
    {
        struct pollfd pollFDs[1];
        memset(pollFDs, 0 , sizeof(pollFDs));
        pollFDs[0].fd = fd;
        pollFDs[0].events = POLLOUT;

        int pollResult = poll(pollFDs, 1, SOCKET_TIMEOUT_MILLIS);
        if (pollResult == 0)
        {
            LOG_WARN("Timeout writing to server, fd={}", fd);
            return -1;
        }
        if (pollResult < 0 || pollFDs[0].revents != POLLOUT)
        {
            LOG_WARN("Error listening to write event, fd={}", fd);
            return -1;
        }

        return send(fd, buf, nByte, 0);
    }
//---------------------------------------------------------------------------------------------------------------------
    ssize_t TimedRead(int fd, void *buf, size_t nByte)
    {
        struct pollfd pollFDs[1];
        memset(pollFDs, 0 , sizeof(pollFDs));
        pollFDs[0].fd = fd;
        pollFDs[0].events = POLLIN;

        int pollResult = poll(pollFDs, 1, SOCKET_TIMEOUT_MILLIS);
        if (pollResult == 0)
        {
            LOG_WARN("Timeout reading from server, fd={}", fd);
            return -1;
        }
        if (pollResult < 0 || pollFDs[0].revents != POLLIN)
        {
            LOG_WARN("Error listening to read event, fd={}", fd);
            return -1;
        }

        return recv(fd, buf, nByte, 0);
    }
//---------------------------------------------------------------------------------------------------------------------
    int ReadBytesToArray(int clientFd, char* data, int size)
    {
        int totalRead = 0;

        while (totalRead < size)
        {
            int tempRead;
            if ((tempRead = TimedRead(clientFd, data + totalRead, size - totalRead)) > 0)
            {
                totalRead += tempRead;
            }
            else 
            {
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
            LOG_ERROR("couldn't serialize message proto");
            return false;
        }

        string outputBuffer(REQUEST_METADATA_SIZE, 0);
        ConvertIntToBytes(protoBuffer.size(), outputBuffer.data(), 0, REQUEST_METADATA_SIZE);
        
        outputBuffer += protoBuffer;

        if (TimedWrite(clientFd, outputBuffer.data(), outputBuffer.size()) != outputBuffer.size())
        {
            LOG_ERROR("couldn't send message to remote fd={}", clientFd);
            return false;
        }
        return true;
    }
//---------------------------------------------------------------------------------------------------------------------
    bool ReadMessageFromRemote(int serverFd, string& message)
    {
        char metaData[REQUEST_METADATA_SIZE];
        int res = ReadBytesToArray(serverFd, metaData, REQUEST_METADATA_SIZE);
        if (res != REQUEST_METADATA_SIZE)
        {
            LOG_ERROR("error: Failed to read metadata from server, fd={}", serverFd);
            return false;
        }

        uint32_t protoSize = ConvertBytesToInt(metaData, REQUEST_METADATA_SIZE);

        string requestMessage(protoSize, 0);
        res = ReadBytesToArray(serverFd, requestMessage.data(), protoSize);
        if (res != protoSize)
        {
            LOG_ERROR("error: Failed to read protobuf message, fd={}", serverFd);
            return false;
        }
        message = requestMessage;
        return true;
    }
//---------------------------------------------------------------------------------------------------------------------
    uint32_t GetServerIndex(const std::string& filePath, uint32_t numServers)
    {        
        if (numServers > 1)
        {
            static hash<string> hasher;
            auto hash_val = hasher(filePath);
            return hash_val % numServers;            
        }
        else
        {
            return 0;
        }
    }
//---------------------------------------------------------------------------------------------------------------------
}
//---------------------------------------------------------------------------------------------------------------------
