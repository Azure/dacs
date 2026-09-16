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

// DACS includes
#include "UploadRequest.h"
#include "DownloadRequest.h"
#include "cache.pb.h"
#include "cache.pb.cc"
#include "CacheUtils.h"
#include "SocketConnection.cpp"
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
using namespace cacheutils;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
bool InitializeConnection(
    /*[in]*/ const std::string& hostName,
    /*[in]*/ int port,
    /*[out]*/ unique_ptr<IConnection> &connection)
{
    int clientSocketFd = -1;
    std::cout << "New connection to server: " << hostName << " Port: " << port << std::endl;
    sockaddr_in sockAddr;
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(static_cast<uint16_t>(port));
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

    connection = make_unique<SocketConnection>(clientSocketFd);
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
        unique_ptr<IConnection> connection;
        if(!InitializeConnection(hostName, port, connection))
        {
            std::cout << "error: Failed to initialize connection to server: " <<  hostName << std::endl;
            exit(1);
        }

        Request request;
        DeleteRequest* deleteRequest = request.mutable_deleterequest();
        deleteRequest->set_groupid(groupId.c_str());

        if(!SendMessageToRemote(connection.get(), request))
        {
            std::cout << "error : couldn't send message to server: " << hostName << std::endl;
            exit(1);
        }

        std::string requestMessage;
        if (!ReadMessageFromRemote(connection.get(), requestMessage))
        {
            std::cout << "error : couldn't read message from server: " <<  hostName << std::endl;
            exit(1);
        }

        DeleteResponse response;
        if (!response.ParseFromString(requestMessage))
        {
            std::cout << "error : Failed to parse protobuf message" << std::endl;
            exit(1);
        }

        if (response.result() != DeleteResponse::SUCCESS)
        {
            std::cout << "delete failed: response.result() != DeleteResponse::SUCCESS" << std::endl;
            exit(1);
        }

        std::cout << "Delete request succeeded for server: " << hostName << std::endl;
    }

    return 0;
}
//---------------------------------------------------------------------------------------------------------------------