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
#include "ServerLocatorCache.h"
#include "LocatorEntry.h"
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
#include <string>
#include <utility>
#include "Metrics.h"
#include "NetworkCounters.h"
#include "ServerArgs.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
void CacheServerMetricsBackgroundThread();

namespace
{
uint32_t GetLocatorEnv(const char* strName, uint32_t nDefault, bool bAllowZero)
{
    const char* strValue = std::getenv(strName);
    if (strValue == nullptr || *strValue == '\0')
    {
        return nDefault;
    }

    char* strEnd = nullptr;
    errno = 0;
    const unsigned long nValue = std::strtoul(strValue, &strEnd, 10);
    if (errno != 0 || strEnd == strValue || *strEnd != '\0' ||
        (!bAllowZero && nValue == 0) || nValue > std::numeric_limits<uint32_t>::max())
    {
        LOG_WARN("Invalid {} value '{}'; using {}", strName, strValue, nDefault);
        return nDefault;
    }

    return static_cast<uint32_t>(nValue);
}
} // namespace
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
        LOG_DEBUG("Request type: {}, response code: {}", eRequestType, std::to_underlying(eResponseCode));
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
                clientFd, eRequestType, std::to_underlying(eResponseCode));
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
        Metrics::RegisterRequest(
            eRequestType, eResponseCode, static_cast<double>(duration.count()), nFileSize);
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
    server_adderss.sin_port = htons(static_cast<uint16_t>(port));
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
    Metrics::SetListenBacklogMax(static_cast<std::size_t>(tcpBacklogSize));
    std::thread(CacheServerMetricsBackgroundThread).detach();

    // main loop accepting connections 
    while (true)
    {
        int clientFd = -1;            
        socklen_t client_len = 0;
        clientFd = accept(server_socket_fd, nullptr, &client_len);
        if (clientFd < 0) 
        {
            LOG_WARN("accept failed on server port, errno: {}:{}", errno, strerror(errno));
            Metrics::IncrementAcceptError(errno);
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
        const size_t nLocatorEntries = kvclient::AddLocatorEntriesToSnapshot(*snapshot);

        LOG_INFO("Saving periodic snapshot with version: {} and {} locator entries",
                 snapshot->version(), nLocatorEntries);
        SnapshotManager::GetInstance().SaveSnapshot(*snapshot);
    }
}
//---------------------------------------------------------------------------------------------------------------------
void CacheServerMetricsBackgroundThread()
{
    LOG_INFO("Starting cache server metrics background thread");

    auto incrementIfMonotonic = [](uint64_t current, uint64_t previous, const char* counterName, auto increment) {
        if (current < previous)
        {
            LOG_WARN("Network counter '{}' decreased (current={} previous={}); skipping delta and resetting baseline",
                     counterName,
                     current,
                     previous);
            return;
        }

        increment(current - previous);
    };

    NetworkCounters previousCounters;
    bool hasProcNetstatBaseline = ReadNetstatCounters(previousCounters);
    bool hasTcpOpensBaseline = ReadTcpOpenCounters(previousCounters.passiveOpens, previousCounters.activeOpens);
    (void)ReadOpenFdCount(previousCounters.openFdCount);

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));

        NetworkCounters currentCounters;
        const bool procNetstatRead = ReadNetstatCounters(currentCounters);
        if (procNetstatRead)
        {
            if (hasProcNetstatBaseline)
            {
                incrementIfMonotonic(currentCounters.listenOverflows,
                                     previousCounters.listenOverflows,
                                     "listenOverflows",
                                     Metrics::IncrementAcceptQueueOverflows);
                incrementIfMonotonic(
                    currentCounters.listenDrops, previousCounters.listenDrops, "listenDrops", Metrics::IncrementListenDrops);
                incrementIfMonotonic(currentCounters.synQueueFullDrops,
                                     previousCounters.synQueueFullDrops,
                                     "synQueueFullDrops",
                                     Metrics::IncrementSynQueueFullDrops);
                incrementIfMonotonic(currentCounters.synCookiesSent,
                                     previousCounters.synCookiesSent,
                                     "synCookiesSent",
                                     Metrics::IncrementSynCookiesSent);
            }
            previousCounters.listenOverflows = currentCounters.listenOverflows;
            previousCounters.listenDrops = currentCounters.listenDrops;
            previousCounters.synQueueFullDrops = currentCounters.synQueueFullDrops;
            previousCounters.synCookiesSent = currentCounters.synCookiesSent;
            hasProcNetstatBaseline = true;
        }

        const bool tcpOpensRead = ReadTcpOpenCounters(currentCounters.passiveOpens, currentCounters.activeOpens);
        if (tcpOpensRead)
        {
            if (hasTcpOpensBaseline)
            {
                incrementIfMonotonic(currentCounters.passiveOpens,
                                     previousCounters.passiveOpens,
                                     "passiveOpens",
                                     Metrics::IncrementPassiveOpens);
            }
            previousCounters.passiveOpens = currentCounters.passiveOpens;
            previousCounters.activeOpens = currentCounters.activeOpens;
            hasTcpOpensBaseline = true;
        }

        const bool openFdRead = ReadOpenFdCount(currentCounters.openFdCount);
        if (openFdRead)
        {
            Metrics::UpdateOpenFdCount(currentCounters.openFdCount);
            previousCounters.openFdCount = currentCounters.openFdCount;
        }

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
    CacheManager::GetInstance().StartCacheCleanupWorker(serverArgs.cacheTTLWindowSeconds);

    InitializeUtils();

    const uint32_t nLocatorNodeId = GetLocatorEnv("TACHYONKV_LOCATOR_NODE_ID", 0, true);
    const uint32_t nLocatorNumNodes = GetLocatorEnv("TACHYONKV_LOCATOR_NUM_NODES", 1, false);
    const char* strLocatorIndex = std::getenv("CACHESERVER_LOCATOR_INDEX");
    const std::string strLocatorIndexBackend = strLocatorIndex != nullptr ? strLocatorIndex : "";
    kvclient::InitServerLocatorCache(strLocatorIndexBackend, nLocatorNodeId, nLocatorNumNodes);
    LOG_INFO("Initialized locator cache: nodeId={}, numNodes={}, index={}",
             nLocatorNodeId,
             nLocatorNumNodes,
             strLocatorIndexBackend.empty() ? "flatmap" : strLocatorIndexBackend);

    if (SnapshotManager::GetInstance().IsSnapshotEnabled())
    {
        auto snapshot = SnapshotManager::GetInstance().LoadSnapshotOnStartup();
        if (snapshot != nullptr)
        {
            LOG_INFO("Loaded snapshot with version: {}", snapshot->version());
            CacheManager::GetInstance().InitializeFromSnapshot(*snapshot);

            const uint64_t nNowMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            if (snapshot->locatorentries_size() > 0)
            {
                // The snapshot carries this server's locator index with each entry's parent and
                // root, so chain lookups work again, including for blocks stored on other servers.
                const size_t nRestored = kvclient::RestoreLocatorEntriesFromSnapshot(*snapshot, nNowMs);
                LOG_INFO("Restored {} of {} locator entries from snapshot",
                         nRestored, snapshot->locatorentries_size());
            }
            else
            {
                // A snapshot without locator entries (from an earlier version, or taken while
                // the locator was empty): rebuild per-key entries from the local KV files.
                uint32_t nKVEntries = 0;
                for (const auto& bucket : snapshot->buckets())
                {
                    for (const auto& entry : bucket.entries())
                    {
                        if (entry.metadata().find("tachyonkv") == entry.metadata().end())
                        {
                            continue;
                        }

                        const uint64_t nExpiresAtMs = static_cast<uint64_t>(entry.expirytimestamp()) * 1000;
                        if (nExpiresAtMs <= nNowMs)
                        {
                            continue;
                        }

                        const ContentReplica replica(
                            nLocatorNodeId,
                            nExpiresAtMs,
                            nNowMs,
                            entry.filesize());
                        kvclient::GetServerLocatorCache().Register(entry.filename(), replica);
                        ++nKVEntries;
                    }
                }
                LOG_INFO("Re-registered {} KV entries in locator cache from snapshot", nKVEntries);
            }

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
        std::thread(SnapshotBackgroundThread, snapshot).detach();
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
    StartServer(port);
}
//---------------------------------------------------------------------------------------------------------------------