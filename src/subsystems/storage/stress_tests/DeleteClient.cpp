///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
// DACS includes
#include "Constants.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "UploadRequest.h"
#include "LockRequest.h"
#include "DownloadRequest.h"
#include "cache.pb.h"
#include "cache.pb.cc"
// End of DACS includes

#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <chrono>
#include <netdb.h>
#include <cstring>
#include <signal.h>
#include <thread>
#include <vector>
#include <fstream>
#include <mutex>
#include <queue>
#include <list>
#include <atomic>
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
uint64_t ConvertBytesToInt(/*[in]*/ char* data, /*[in]*/ int size)
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
void ConvertIntToBytes(/*[in]*/ uint64_t source, /*[out]*/ char* destination, /*[in]*/ int index, /*[in]*/ int size)
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
int ReadBytesToArray(/*[in]*/ int clientFd, /*[out]*/ char* data, /*[in]*/ int size)
{
    int totalRead = 0;

    while (totalRead < size)
    {
        int tempRead;
        if ((tempRead = read(clientFd, data + totalRead, size - totalRead)) > 0)
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
bool InitializeConnection(
    /*[in]*/ const std::string& hostName,
    /*[in]*/ int port,
    /*[out]*/ int& clientSocketFd)
{
    std::cout << "New connection to server: " << hostName << " Port: " << port << std::endl;

    sockaddr_in sockAddr;
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(port);
    if( (inet_pton(AF_INET, hostName.c_str(), &(sockAddr.sin_addr))) < 0)
    {
        std::cout << "error: inet_pton error: " << errno << " message: " << ::strerror(errno) << std::endl;
        return false;
    }
    
    if ( (clientSocketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        std::cout << "error: unable to create a socket connection: " << errno << " message: " << ::strerror(errno) << std::endl;
        return false;
    }
    
    if (connect(clientSocketFd, (sockaddr *) &( sockAddr ), sizeof(sockAddr) ) < 0 )
    {
        std::cout << "connection failed error: " << errno << " message: " << ::strerror(errno) << std::endl;
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool SendMessageToRemote(/*[in]*/ int clientFd, /*[in]*/ const ::google::protobuf::Message& message)
{
    std::string protoBuffer;
    if (!message.SerializeToString(&protoBuffer))
    {
        std::cout << "error: couldn't serialize message proto" << std::endl;
        return false;
    }

    std::string outputBuffer(REQUEST_METADATA_SIZE, 0);
    ConvertIntToBytes(protoBuffer.size(), outputBuffer.data(), 0, REQUEST_METADATA_SIZE);
    
    outputBuffer += protoBuffer;

    if (write(clientFd, outputBuffer.data(), outputBuffer.size()) != outputBuffer.size())
    {
        std::cout << "error: couldn't write message to remote fd: " << clientFd << std::endl;
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool ReadMessageFromRemote(/*[in]*/ int serverFd, /*[in]*/ string& message)
{
    char metaData[REQUEST_METADATA_SIZE];
    int res = ReadBytesToArray(serverFd, metaData, REQUEST_METADATA_SIZE);
    if (res != REQUEST_METADATA_SIZE)
    {
        std::cout << "error : Failed to read metadata from server" << std::endl;
        return false;
    }

    uint32_t protoSize = ConvertBytesToInt(metaData, REQUEST_METADATA_SIZE);
    string requestMessage(protoSize, 0);

    res = ReadBytesToArray(serverFd, requestMessage.data(), protoSize);
    if (res != protoSize)
    {
        std::cout << "Failed to read protobuf message" << std::endl;
        return false;
    }

    message = requestMessage;
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
// Server hostnames, port and group_id are provided via environment variables
//---------------------------------------------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // Expects: ServerList, port, group_id
    if (argc < 4)
    {
        std::cout << "Too few arguments provided, terminating" << std::endl;
        exit(1);
    }

    std::string serverHostNames = argv[1];
    int port = atoi(argv[2]);
    std::string groupId = argv[3];

    // Get the server configuration list
    std::vector<std::string> serverHostNamesList;
    std::size_t pos = 0;
    std::string token;
    // pad the delimiter at the end to make the logic below work
    serverHostNames += "#";

    // Use # as the delimiter
    while((pos = serverHostNames.find("#")) != std::string::npos)
    {
        token = serverHostNames.substr(0, pos);
        std::cout << "Adding server host: " << token << std::endl;
        serverHostNamesList.push_back(token);
        serverHostNames.erase(0, pos + 1); // Using 1 here since delimiter is a single character
    }

    // Loop through each server, setup a connection and issue the delete request
    for(auto& hostName : serverHostNamesList)
    {
        int clientSocketFd = -1;
        if(!InitializeConnection(hostName, port, clientSocketFd))
        {
            std::cout << "error: Failed to initialize connection to server: " <<  hostName << std::endl;
            exit(1);
        }

        Request request;
        DeleteRequest* deleteRequest = request.mutable_deleterequest();
        deleteRequest->set_groupid(groupId.c_str());

        if(!SendMessageToRemote(clientSocketFd, request))
        {
            std::cout << "error : couldn't send message to server: " << hostName << std::endl;
            close(clientSocketFd);
            exit(1);
        }

        std::string requestMessage;
        if (!ReadMessageFromRemote(clientSocketFd, requestMessage))
        {
            std::cout << "error : couldn't read message from server: " <<  hostName << std::endl;
            close(clientSocketFd);
            exit(1);
        }

        DeleteResponse response;
        if (!response.ParseFromString(requestMessage))
        {
            std::cout << "error : Failed to parse protobuf message" << std::endl;
            close(clientSocketFd);
            exit(1);
        }

        if (response.result() != DeleteResponse::SUCCESS)
        {
            std::cout << "delete failed: response.result() != DeleteResponse::SUCCESS" << std::endl;
            close(clientSocketFd);
            exit(1);
        }

        std::cout << "Delete request succeeded for server: " << hostName << std::endl;
        // Close the connection
        close(clientSocketFd);
    }

    return 0;
}
//---------------------------------------------------------------------------------------------------------------------