///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "Constants.h"
#include "Utils.h"
#include "SocketUtils.h"
#include "UploadRequest.h"
#include "LockRequest.h"
#include "DownloadRequest.h"
#include "PutAttributeRequest.h"
#include "GetAttributeRequest.h"
#include "cache.pb.h"
#include "Logger.h"
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
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
#include <errno.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
void HandleSocket(int epollFd)
{
    struct epoll_event epollEvents[1];
    while (true)
    {
        int nEvents = epoll_wait(epollFd, epollEvents, 1, -1);
        if (nEvents <= 0) {
            LOG_CRITICAL("epoll_wait failed, errno: {}", errno);
            exit(-1);
        }

        if (nEvents > 1) {
            LOG_CRITICAL("received more than maxuimum(1) events");
            exit(-1);
        }

        // Check for errors and then start worker
        int clientFd = -1;        
        if (epollEvents[0].events & EPOLLRDHUP)
        {
            LOG_INFO("Connection closed by client");
            close (epollEvents[0].data.fd);
            continue;
        }
        else if (epollEvents[0].events & EPOLLIN)
        {
            clientFd = epollEvents[0].data.fd;

            // remove fd from epoll list and then start using the fd
            if (epoll_ctl(epollFd, EPOLL_CTL_DEL , clientFd, nullptr) == -1) {
                LOG_CRITICAL("Failed to remove  fd from epoll list");
                exit(-1);
            }    
        }
        else
        {
            LOG_INFO("Received unexpected event flag {} from epoll_wait (could be client close)",
                (uint32_t)epollEvents[0].events);
            close (epollEvents[0].data.fd);
            continue;
        }

        // TODO: read with a timeout
        // Get the request protobuf size
        char metaData[REQUEST_METADATA_SIZE];
        int res = ReadBytesToArray(clientFd, metaData, REQUEST_METADATA_SIZE);
        if (res != REQUEST_METADATA_SIZE)
        {
            LOG_WARN("Failed to read request metadata from client, fd={}", clientFd);
            close(clientFd);
            continue;
        }

        uint32_t protoSize = ConvertBytesToInt(metaData, REQUEST_METADATA_SIZE);
        LOG_DEBUG("Cache server protobuf size: {}", protoSize);

        string requestMessage(protoSize, 0);
        res = ReadBytesToArray(clientFd, requestMessage.data(), protoSize);
        if (res != protoSize)
        {
            LOG_WARN("Failed to read protobuf message, fd={}", clientFd);
            close(clientFd);
            continue;
        }

        Request request;
        if (!request.ParseFromString(requestMessage))
        {
            LOG_WARN("Failed to parse protobuf message, fd={}", clientFd);
            close(clientFd);
            continue;
        }

        bool bReuse = false;
        if (request.type_case() == Request::kDownloadrequest)
        {
            bReuse = DownloadFile(clientFd, request.downloadrequest());
        }
        else if (request.type_case() == Request::kUploadrequest)
        {
            bReuse = UploadFile(clientFd, request.uploadrequest());
        } 
        else if (request.type_case() == Request::kLockrequest)
        {
            bReuse = LockFile(clientFd, request.lockrequest());
        }
        else if (request.type_case() == Request::kGetattributerequest)
        {
            bReuse = HandleGetAttribute(clientFd, request.getattributerequest());
        }
        else if (request.type_case() == Request::kPutattributerequest)
        {
            bReuse = HandlePutAttribute(clientFd, request.putattributerequest());
        }
        else
        {
            LOG_ERROR("unkown req type: {}, close fd={}", request.type_case(), clientFd);
            close(clientFd);
            continue;
        }

        if (bReuse)
        {
            // To re-use the client connection, put it back into the epoll list
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
            ev.data.fd = clientFd;
            if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev) == -1) {
                LOG_WARN("Failed to re-insert client fd={} to epoll list", clientFd);
                close(clientFd);
            }    
        }
        else
        {
            LOG_DEBUG("close fd={}", clientFd);
            close(clientFd);
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
void StartServer(int port)
{
    int server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd < 0)
    {
        LOG_CRITICAL("Failed to create server socket");
        exit(-1);
    }

    int enable;
    if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) != 0)
    {
        LOG_CRITICAL("Failed to set SO_REUSEADDR");
        close(server_socket_fd);
        exit(-1);
    }

    struct sockaddr_in server_adderss;
    server_adderss.sin_family = AF_INET;
    server_adderss.sin_port = htons(port);
    server_adderss.sin_addr.s_addr = INADDR_ANY;
    if (bind(server_socket_fd, (struct sockaddr *)&server_adderss, sizeof(server_adderss)) != 0)
    {
        LOG_CRITICAL("Failed to bind to given port");
        close(server_socket_fd);
        exit(-1);
    }
    
    if (listen(server_socket_fd, TCP_BACKLOG_SIZE) != 0)
    {
        LOG_CRITICAL("Failed to listen on server port");
        close(server_socket_fd);
        exit(-1);
    }

    struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);

    // configure common epoll for workers
    int epollWorkerFd = epoll_create1(0);
    if (epollWorkerFd < 0)
    {
        LOG_CRITICAL("Failed to create epoll for workers");
        close(server_socket_fd);
        exit(-1);
    }

    // start worker threads
    std::vector<std::thread *> threads;
    for (int i = 0; i < THREAD_POOL_SIZE; ++i)
    {
        auto thread = new std::thread([epollWorkerFd]() {
            HandleSocket(epollWorkerFd);
        });
        threads.push_back(thread);
    }

    LOG_INFO("Started server on port {}", port);

    // main loop accepting connections 
    while (true)
    {
        int clientFd = -1;            
        socklen_t client_len = 0;
        clientFd = accept(server_socket_fd, nullptr, &client_len);
        if (clientFd < 0) 
        {
            LOG_WARN("accept failed on server port");
            continue;
        }
        int enable = 1;
        if (setsockopt(clientFd, SOL_TCP, TCP_NODELAY, &enable, sizeof(enable)) != 0)
        {
            LOG_ERROR("Error setting TCP_NODELAY on client socket");
            close(clientFd);
            continue;
        }

        // insert client fd to worker epoll list
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
        ev.data.fd = clientFd;
        if (epoll_ctl(epollWorkerFd, EPOLL_CTL_ADD, clientFd, &ev) == -1) {
            LOG_CRITICAL("Failed to insert client fd to epoll list");
            close(clientFd);
            exit(-1);
        }    
    }
}
//---------------------------------------------------------------------------------------------------------------------
int main(int argc, char *argv[])
{

    LoggingApi::Init();

    if (argc < 2) 
    {
        LOG_CRITICAL("Usage: ./CacheServer <port> <cache folder root> <cache size in bytes> <Num attributes limit>");
        return -1;
    }
    int port = atoi(argv[1]);

    if (argc > 2)
    {
        SetCacheRootPath(argv[2]);
    }

    if (argc > 3)
    {
        SetCacheSizeLimit(stoull(argv[3]));
    }

    if (argc > 4)
    {
        SetAttributeCacheLimit(stoull(argv[4]));
    }

    Initialize();

    // ignore signal when write to socket fails, this is handled by -1 return value of the write call
    signal(SIGPIPE, SIG_IGN); 
    StartServer(port);
}
//---------------------------------------------------------------------------------------------------------------------