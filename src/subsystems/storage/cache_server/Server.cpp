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

#include "Constants.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "HandleRequest.h"
#include "CacheManager.h"
#include "SnapshotManager.h"
#include "SocketConnection.h"
#include "IBManager.h"
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
#include <cstdlib>
#include <limits>
#include "Metrics.h"
#include "ServerArgs.h"
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
            LOG_CRITICAL("epoll_wait failed, errno: {}:{}", errno, strerror(errno));
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
                LOG_CRITICAL("Failed to remove  fd from epoll list, errno: {}:{}", errno, strerror(errno));
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
        
        Metrics::IncrementActiveConnections();
        auto start_time = std::chrono::steady_clock::now();

        // TODO: read with a timeout
        // Get the request protobuf size
        SocketConnection socketConnection(clientFd);
        Request::TypeCase eRequestType = Request::TypeCase::TYPE_NOT_SET;
        ResponseCode eResponseCode = ResponseCode::SUCCESS;
        std::size_t nFileSize = 0;
        bool bReuse = HandleRequest(socketConnection, eRequestType, eResponseCode, nFileSize);
        LOG_DEBUG("Request type: {}, response code: {}", eRequestType, eResponseCode);
        if (bReuse)
        {
            // To re-use the client connection, put it back into the epoll list
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
            ev.data.fd = clientFd;
            if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev) == -1) {
                LOG_WARN("Failed to re-insert client fd={} to epoll list, errno: {}:{}",
                    clientFd, errno, strerror(errno));
                close(clientFd);
            }
        }
        else
        {
            LOG_INFO("Close fd={} after request processing, request type: {}, response code: {}",
                clientFd, eRequestType, eResponseCode);
            if (close(clientFd) < 0)
            {
                LOG_WARN("Failed to close client fd={}, errno: {}:{}", clientFd, errno, strerror(errno));
            }

            if(eResponseCode == ResponseCode::SUCCESS)
            {
                eResponseCode = ResponseCode::NETWORK_ERROR;
            }
        }

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        Metrics::RegisterRequest(eRequestType, eResponseCode, duration.count(), nFileSize);
        Metrics::DecrementActiveConnections();
    }
}
//---------------------------------------------------------------------------------------------------------------------
void StartServer(int port)
{
    int threadPoolSize = DEFAULT_THREAD_POOL_SIZE;
    char* threadPoolSizeStr = std::getenv("CACHESERVER_THREADPOOL_SIZE");
    if (threadPoolSizeStr != nullptr)
    {
        threadPoolSize = std::stoi(threadPoolSizeStr);
    }

    int socketSndbufSize = DEFAULT_SOCKET_BUF_SIZE;
    char* socketSndbufSizeStr = std::getenv("CACHESERVER_SOCKET_SNDBUF_SIZE");
    if (socketSndbufSizeStr != nullptr)
    {
        socketSndbufSize = std::stoi(socketSndbufSizeStr);
    }

    int socketRcvbufSize = DEFAULT_SOCKET_BUF_SIZE;
    char* socketRcvbufSizeStr = std::getenv("CACHESERVER_SOCKET_RCVBUF_SIZE");
    if (socketRcvbufSizeStr != nullptr)
    {
        socketRcvbufSize = std::stoi(socketRcvbufSizeStr);
    }

    int tcpBacklogSize = DEFAULT_TCP_BACKLOG_SIZE;
    char* tcpBacklogSizeStr = std::getenv("CACHESERVER_TCP_BACKLOG_SIZE");
    if (tcpBacklogSizeStr != nullptr && strlen(tcpBacklogSizeStr) > 0)
    {
        char* pEndPtr = nullptr;
        errno = 0;
        long int nValue = std::strtol(tcpBacklogSizeStr, &pEndPtr, 10);
        if (errno != 0 || pEndPtr == tcpBacklogSizeStr || *pEndPtr != '\0' ||
            nValue <= 0 || nValue > MAX_TCP_BACKLOG_SIZE)
        {
            LOG_WARN("Invalid CACHESERVER_TCP_BACKLOG_SIZE: {}, using default {}", tcpBacklogSizeStr, tcpBacklogSize);
        }
        else
        {
            tcpBacklogSize = static_cast<int>(nValue);
        }
    }

    int server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd < 0)
    {
        LOG_CRITICAL("Failed to create server socket, errno: {}:{}", errno, strerror(errno));
        exit(-1);
    }

    int enable = 1;
    if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) != 0)
    {
        LOG_CRITICAL("Failed to set SO_REUSEADDR, errno: {}:{}", errno, strerror(errno));
        close(server_socket_fd);
        exit(-1);
    }

    struct sockaddr_in server_adderss;
    server_adderss.sin_family = AF_INET;
    server_adderss.sin_port = htons(port);
    server_adderss.sin_addr.s_addr = INADDR_ANY;
    if (bind(server_socket_fd, (struct sockaddr *)&server_adderss, sizeof(server_adderss)) != 0)
    {
        LOG_CRITICAL("Failed to bind to given port: {}, errno: {}:{}", port, errno, strerror(errno));
        close(server_socket_fd);
        exit(-1);
    }
    
    if (listen(server_socket_fd, tcpBacklogSize) != 0)
    {
        LOG_CRITICAL("Failed to listen on server port: {}, errno: {}:{}", port, errno, strerror(errno));
        close(server_socket_fd);
        exit(-1);
    }

    struct sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);

    // configure common epoll for workers
    int epollWorkerFd = epoll_create1(0);
    if (epollWorkerFd < 0)
    {
        LOG_CRITICAL("Failed to create epoll for workers, errno: {}:{}", errno, strerror(errno));
        close(server_socket_fd);
        exit(-1);
    }

    // start worker threads
    std::vector<std::thread *> threads;
    for (int i = 0; i < threadPoolSize; ++i)
    {
        auto thread = new std::thread([epollWorkerFd]() {
            HandleSocket(epollWorkerFd);
        });
        threads.push_back(thread);
    }

    LOG_INFO("Started server on port: {}, threadpool: {}, tcp backlog: {}", port, threadPoolSize, tcpBacklogSize);

    // main loop accepting connections 
    while (true)
    {
        int clientFd = -1;            
        socklen_t client_len = 0;
        clientFd = accept(server_socket_fd, nullptr, &client_len);
        if (clientFd < 0) 
        {
            LOG_WARN("accept failed on server port, errno: {}:{}", errno, strerror(errno));
            continue;
        }
        int enable = 1;
        if (setsockopt(clientFd, SOL_TCP, TCP_NODELAY, &enable, sizeof(enable)) != 0)
        {
            LOG_ERROR("Error setting TCP_NODELAY on client socket, errno: {}:{}", errno, strerror(errno));
            close(clientFd);
            continue;
        }

        if (socketSndbufSize > 0)
        {
            if (setsockopt(clientFd, SOL_SOCKET, SO_SNDBUF, &socketSndbufSize, sizeof(socketSndbufSize)) != 0)
            {
                LOG_WARN("Failed to set SO_SNDBUF={} on client socket, errno: {}:{}", socketSndbufSize, errno, strerror(errno));
            }
        }
        if (socketRcvbufSize > 0)
        {
            if (setsockopt(clientFd, SOL_SOCKET, SO_RCVBUF, &socketRcvbufSize, sizeof(socketRcvbufSize)) != 0)
            {
                LOG_WARN("Failed to set SO_RCVBUF={} on client socket, errno: {}:{}", socketRcvbufSize, errno, strerror(errno));
            }
        }

        if (socketSndbufSize > 0 || socketRcvbufSize > 0)
        {
            int actualSndbuf = 0, actualRcvbuf = 0;
            socklen_t optlen = sizeof(actualSndbuf);
            if (getsockopt(clientFd, SOL_SOCKET, SO_SNDBUF, &actualSndbuf, &optlen) == 0 &&
                getsockopt(clientFd, SOL_SOCKET, SO_RCVBUF, &actualRcvbuf, &optlen) == 0)
            {
                LOG_INFO("Socket buffer sizes for clientFd={}: SO_SNDBUF requested={} actual={}, SO_RCVBUF requested={} actual={}",
                    clientFd, socketSndbufSize, actualSndbuf, socketRcvbufSize, actualRcvbuf);
            }
        }

        // insert client fd to worker epoll list
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
        ev.data.fd = clientFd;
        if (epoll_ctl(epollWorkerFd, EPOLL_CTL_ADD, clientFd, &ev) == -1) {
            LOG_CRITICAL("Failed to insert client fd to epoll list, errno: {}:{}", errno, strerror(errno));
            close(clientFd);
            exit(-1);
        }    
    }
}
//---------------------------------------------------------------------------------------------------------------------
void SnapshotBackgroundThread(std::shared_ptr<snapshot::Snapshot> startingSnaphot)
{
    LOG_INFO("Starting snapshot background thread");
    if (startingSnaphot != nullptr && !SnapshotManager::GetInstance().WaitForSnapshotConsistencyScan())
    {
        LOG_INFO("Scanning for snapshot consistency in background thread");
        CacheManager::GetInstance().ScanForSnapshotConsistency(*startingSnaphot);
        LOG_INFO("Snapshot consistency scan complete in background thread");
    }

    startingSnaphot = nullptr; // deallocate the starting snapshot
    LOG_INFO("Starting periodic snapshots in background thread. Interval: {} seconds", 
        SnapshotManager::GetInstance().GetSnapshotIntervalInSeconds());
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(SnapshotManager::GetInstance().GetSnapshotIntervalInSeconds()));

        std::shared_ptr<snapshot::Snapshot> snapshot = CacheManager::GetInstance().TakeSnapshot();
        STORAGE_ASSERT(snapshot != nullptr, "Failed to take snapshot");

        LOG_INFO("Saving periodic snapshot with version: {}", snapshot->version());
        SnapshotManager::GetInstance().SaveSnapshot(*snapshot);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void CacheServerSizeBackgroundThread()
{
    LOG_INFO("Starting cache server size background thread");
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        size_t cacheSize = CacheManager::GetInstance().GetCacheServerSize();
        size_t cacheSizeLimit = CacheManager::GetInstance().GetCacheServerSizeLimit();
        double cacheSizePercentage = CacheManager::GetInstance().GetCacheServerUtilizationPercentage();
        LOG_INFO("{{CacheSize: {}, CacheSizeLimit: {}, CacheSizePercentage: {}}}",
                 cacheSize,
                 cacheSizeLimit,
                 cacheSizePercentage);
        Metrics::UpdateCacheSize(cacheSize);
    }
}
//---------------------------------------------------------------------------------------------------------------------
int main(int argc, char *argv[])
{

    LoggingApi::Init();
    LoggingApi::FlushCachedLog();
    LOG_INFO("Cache Server build commit hash " BUILD_GIT_COMMIT_HASH " built on " __DATE__);

    ServerArgs serverArgs;
    std::string usageMessage;
    ParseServerArgsResult parseResult = ParseServerArgs(argc, argv, serverArgs, usageMessage);
    if (parseResult == ParseServerArgsResult::HelpRequested)
    {
        std::cout << usageMessage << std::endl;
        return 0;
    }
    if (parseResult == ParseServerArgsResult::Error)
    {
        std::cout << usageMessage << std::endl;
        LOG_CRITICAL("{}", usageMessage);
        return -1;
    }

    int port = serverArgs.port;
    SetCacheRootPath(serverArgs.cacheRootPath);
    CacheManager::GetInstance().SetCacheSizeLimit(serverArgs.cacheSizeLimitBytes);
    CacheManager::GetInstance().SetCacheTTLWindowSize(serverArgs.cacheTTLWindowSeconds);

    InitializeUtils();

    std::thread* snapshotThread = nullptr;
    if (SnapshotManager::GetInstance().IsSnapshotEnabled())
    {
        auto snapshot = SnapshotManager::GetInstance().LoadSnapshotOnStartup();
        if (snapshot != nullptr)
        {
            LOG_INFO("Loaded snapshot with version: {}", snapshot->version());
            CacheManager::GetInstance().InitializeFromSnapshot(*snapshot);

            if (SnapshotManager::GetInstance().WaitForSnapshotConsistencyScan())
            {
                LOG_INFO("Wait for snapshot consistency scan enabled, starting it now.");
                CacheManager::GetInstance().ScanForSnapshotConsistency(*snapshot);
                LOG_INFO("Snapshot consistency scan complete");
            }
        }
        else
        {
            LOG_INFO("Snapshot enabled but no snapshot found, will start with empty cache");
            ResetCacheFolder();
            CleanupFolder(GetSnapshotFolderPath());
        }

        // Start the snapshot background thread
        snapshotThread = new std::thread(SnapshotBackgroundThread, snapshot);
    }
    else
    {
        LOG_INFO("Snapshot disabled, will start with empty cache");
        ResetCacheFolder();
        CleanupFolder(GetSnapshotFolderPath());
    }

    if (!IBManager::GetInstance().Init())
    {
        LOG_INFO("Failed to initialize infiniband, will proceed without IB support");
    }

    // ignore signal when write to socket fails, this is handled by -1 return value of the write call
    signal(SIGPIPE, SIG_IGN);
    Metrics::Init();
    std::thread* cacheServerSizeThread = new std::thread(CacheServerSizeBackgroundThread);
    StartServer(port);
}
//---------------------------------------------------------------------------------------------------------------------