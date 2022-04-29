///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "ConnectionManager.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptConfig.h"
#include "StorageDlsymWrapper.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <dlfcn.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <netdb.h>
#include <mutex>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
ConnectionManager::ConnectionManager(int serverPort, bool useLocalServer, const std::vector<std::string>& cacheServers)
    : m_bReuseServerConnections(StorageInterceptConfig::GetInstance().GetCacheReuseServerConnections())
{
    if (useLocalServer)
    {
        m_numServerNodes = 1;
        LOG_INFO("Using 127.0.0.1 as cache server");

        struct addrinfo *result;
        int ret = getaddrinfo("127.0.0.1", NULL, NULL, &result);
        if (ret != 0)
        {
            LOG_CRITICAL("Couldn't create 127.0.0.1 address");
            throw "Couldn't create 127.0.0.1 address";
        }

        sockaddr_in structSockAddr; 
        structSockAddr.sin_addr = ((sockaddr_in *)result->ai_addr)->sin_addr; 
        structSockAddr.sin_family = AF_INET;
        structSockAddr.sin_port = htons(serverPort);

        m_vecCacheServers.push_back(structSockAddr);        
        freeaddrinfo(result);
    }
    else
    {
        m_numServerNodes = cacheServers.size();
        if (m_numServerNodes < 1)
        {
            LOG_CRITICAL("No remote cache servers configured");
            throw "No remote cache servers configured";
        }

        for (const string& nodeEntry : cacheServers)
        {
            struct addrinfo *result;
            int ret = getaddrinfo(nodeEntry.c_str(), NULL, NULL, &result);
            if (ret != 0)
            {
                LOG_CRITICAL("Couldn't resolve server address: {}", nodeEntry);
                throw "Couldn't resolve server address";
            }

            sockaddr_in structSockAddr; 
            structSockAddr.sin_addr = ((sockaddr_in *)result->ai_addr)->sin_addr; 
            structSockAddr.sin_family = AF_INET;
            structSockAddr.sin_port = htons(serverPort);

            m_vecCacheServers.push_back(structSockAddr);        
            freeaddrinfo(result);
            LOG_INFO("Added cache server: {}", nodeEntry)
        }
    }

    m_vecFdQueue.resize(m_numServerNodes);

    // Simple hack as resize() on vector<mutex> does not work due to move requirements  
    vector<mutex> initMutices(m_numServerNodes);
    m_vecLocks.swap(initMutices);

    this->m_nMaxConnsPerServer = (MAX_CONNECTIONS_REUSE_SIZE + m_numServerNodes - 1) / m_numServerNodes;
    LOG_INFO("Initialized Cache ConnectionManager. serverPort: {}, m_numServerNodes: {}, m_nMaxConnsPerServer: {},"
        " m_bReuseServerConnections: {}",
        serverPort,
        m_numServerNodes,
        m_nMaxConnsPerServer,
        m_bReuseServerConnections ? "true" : "false"
        );
}
//---------------------------------------------------------------------------------------------------------------------
ConnectionManager::~ConnectionManager()
{
    LOG_INFO("Purge all open connections");
    for (auto queueFd : m_vecFdQueue)
    {
        while (!queueFd.empty())
        {
            int serverFd = queueFd.front();
            queueFd.pop();
            LOG_DEBUG("Cleanup server fd={}", serverFd);
            StorageDlsymWrapper::close(serverFd);
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
int ConnectionManager::InitFreshConnection(int serverIndex)
{
    // TODO: Retry on failures

    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0)
    {
        LOG_CRITICAL("Error creating a socket fd at client: {}-{}", errno, strerror(errno));
        return -1;
    }
    if (connect(serverFd, (sockaddr * ) &(m_vecCacheServers[serverIndex]), sizeof(sockaddr)) < 0)
    {
        LOG_CRITICAL("Error during socket connect: {}-{}", errno, strerror(errno));
        StorageDlsymWrapper::close(serverFd);
        return -1;
    }

    int enable = 1;
    if (setsockopt(serverFd, SOL_TCP, TCP_NODELAY, &enable, sizeof(enable)) != 0)
    {
        LOG_CRITICAL("Error setting TCP_NODELAY on client socket: {}-{}", errno, strerror(errno));
        StorageDlsymWrapper::close(serverFd);
        return -1;
    }

    LOG_INFO("Created new server connection, fd={}, serverIndex={}", serverFd, serverIndex);
    return serverFd;
}
//---------------------------------------------------------------------------------------------------------------------
int ConnectionManager::GetFd(int serverIndex)
{
    if (m_bReuseServerConnections)
    {
        // First check the connection pool for valid connections
        lock_guard queueLock(m_vecLocks[serverIndex]);
        while (!m_vecFdQueue[serverIndex].empty())
        {
            int serverFd = m_vecFdQueue[serverIndex].front();
            m_vecFdQueue[serverIndex].pop();
            if (IsValidConnection(serverFd))
            {
                LOG_DEBUG("Reusing server fd={}", serverFd);
                return serverFd;
            }
            
            LOG_DEBUG("Close server fd={}", serverFd);
            StorageDlsymWrapper::close(serverFd);
        }
    }

    return InitFreshConnection(serverIndex); 
}
//---------------------------------------------------------------------------------------------------------------------
void ConnectionManager::ReleaseFd(int serverIndex, int serverFd, bool errorFd)
{
    if (m_bReuseServerConnections && !errorFd && IsValidConnection(serverFd))
    {
        lock_guard queueLock(m_vecLocks[serverIndex]);
        if (m_vecFdQueue[serverIndex].size() < m_nMaxConnsPerServer)
        {
            LOG_DEBUG("Add to reuse pool, fd={}", serverFd);
            m_vecFdQueue[serverIndex].push(serverFd);
        }
        else
        {
            LOG_DEBUG("Close server fd={}, reuse queue is full", serverFd);
            StorageDlsymWrapper::close(serverFd);
        }
    }
    else
    {
        LOG_DEBUG("Close server fd={}", serverFd);
        StorageDlsymWrapper::close(serverFd);
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool ConnectionManager::IsValidConnection(int connectionFd)
{
    if (recv(connectionFd, NULL, 1, MSG_PEEK | MSG_DONTWAIT) == 0)
    {
        LOG_DEBUG("Found stale fd={}", connectionFd);
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
