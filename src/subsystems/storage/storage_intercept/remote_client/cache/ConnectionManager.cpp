///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "ConnectionManager.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptConfig.h"
#include "StorageLibcApiWrapper.h"
#include "SocketConnection.h"
#include "IBConnection.h"
#include "cache.pb.h"
#include "CacheUtils.h"
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
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
using namespace cache;
using namespace cacheutils;
using namespace ibutils;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
ConnectionManager::ConnectionManager(
    int serverPort,
    bool useLocalServer,
    const std::vector<std::string>& cacheServers)
    :  m_ServerPort(serverPort)
    ,  m_nMaxConnectionsReuseSize(StorageInterceptConfig::GetInstance().GetCacheMaxConnectionsReuseSize())
    ,  m_bReuseServerConnections(StorageInterceptConfig::GetInstance().GetCacheReuseServerConnections())
    ,  m_bUseInfiniband(StorageInterceptConfig::GetInstance().GetCacheIBEnabled())
    ,  m_ibDevice{0}
{
    if (useLocalServer)
    {
        m_numServerNodes = 1;

        // Infiniband is not necessary when using localhost
        m_bUseInfiniband = false;
        m_vecCacheServers.push_back("127.0.0.1");
        LOG_INFO("Using 127.0.0.1 as cache server");
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
            m_vecCacheServers.push_back(nodeEntry);
            LOG_INFO("Added cache server={}", nodeEntry)
        }
    }

    // Hack as resize() on vector<queue<unique_ptr>> does not work due to move requirements
    std::vector<std::queue<std::unique_ptr<IConnection>>> initConnectionQueue(m_numServerNodes);
    m_vecConnectionQueue.swap(initConnectionQueue);

    // Simple hack as resize() on vector<mutex> does not work due to move requirements  
    vector<mutex> initMutices(m_numServerNodes);
    m_vecLocks.swap(initMutices);

    if (m_bUseInfiniband)
    {
        InitInfiniband();
    }

    this->m_nMaxConnsPerServer = (m_nMaxConnectionsReuseSize + m_numServerNodes - 1) / m_numServerNodes;
    LOG_INFO("Initialized Cache ConnectionManager. serverPort={} m_numServerNodes={}"
        " m_nMaxConnsPerServer={} m_bReuseServerConnections={} m_bUseInfiniband={}",
        serverPort,
        m_numServerNodes,
        m_nMaxConnsPerServer,
        m_bReuseServerConnections ? "true" : "false",
        m_bUseInfiniband ? "true" : "false");
}
//---------------------------------------------------------------------------------------------------------------------
ConnectionManager::~ConnectionManager()
{
    LOG_INFO("Purge all open connections");
    for (auto &queueFd : m_vecConnectionQueue)
    {
        while (!queueFd.empty())
        {
            auto serverConnection = move(queueFd.front());
            queueFd.pop();
        }
    }

    m_ibDevice.pd.reset(nullptr);
    m_ibDevice.context.reset(nullptr);
}
//---------------------------------------------------------------------------------------------------------------------
bool ConnectionManager::InitInfiniband()
{
    // Ensure ibv_fork_init() is called only once per process
    static std::once_flag onceFlag;

    // Reset IB flag and enable it only if the IB setup succeeds
    m_bUseInfiniband = false;
    std::call_once(onceFlag, [](){
        int rc = ibv_fork_init();
        if (rc != 0)
        {
            LOG_CRITICAL("ibv_fork_init() failed error={},{}", rc, strerror(errno));
        }
    });

    // TODO: check status of ibv_fork_init() return value, not sure if we need to terminate the process

    unique_ibv_context context;
    bool bDeviceFound = GetIBDeviceInfo(m_ibDevice.lid, m_ibDevice.port, m_ibDevice.mtu, context);
    if (!bDeviceFound)
    {
        LOG_WARN("No active IB ports found");
        return false;
    }

    unique_ibv_pd pd(ibv_alloc_pd(context.get()), FreeIbvPd);
    if (!pd)
    {
        LOG_WARN("ibv_alloc_pd failed error={}", strerror(errno));
        return false;
    }

    LOG_INFO("IB Init successful");
    m_ibDevice.context = move(context);
    m_ibDevice.pd = move(pd);
    m_bUseInfiniband = true;
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<IConnection> ConnectionManager::SetupIBConnections(int serverFd, int serverIndex)
{
    Request request;
    IBInitRequest* ibRequest = request.mutable_ibinitrequest();
    ibRequest->set_requestid(cacheutils::GetCorrelationId());
    ibRequest->set_clientibport(m_ibDevice.port);
    ibRequest->set_clientlid(m_ibDevice.lid);
    ibRequest->set_clientmtu(m_ibDevice.mtu);
    auto clientQpns = ibRequest->mutable_clientqpn();
    clientQpns->Clear();
    LOG_INFO_REQID("SetupIBConnections: serverFd={} serverIndex={}", *ibRequest, serverFd, serverIndex);

    vector<unique_ibv_comp_channel> vectorEventChannels;
    vector<unique_ibv_cq> vectorCqs;
    vector<unique_ibv_qp> vectorQps;
    for (int qpn = 0; qpn < IB_CONNECTIONS_PER_SERVER; qpn++)
    {
        unique_ibv_comp_channel compChannel(ibv_create_comp_channel(m_ibDevice.context.get()), FreeIbvCompChannel);
        if (!compChannel)
        {
            LOG_ERROR_REQID("ibv_create_comp_channel failed error={}", *ibRequest, strerror(errno));
            close(serverFd);
            return nullptr;
        }

        unique_ibv_cq cq(ibv_create_cq(m_ibDevice.context.get(), 2, NULL, compChannel.get(), 0), FreeIbvCq);
        if (!cq)
        {
            LOG_ERROR_REQID("ibv_create_cq failed error={}", *ibRequest, strerror(errno));
            close(serverFd);
            return nullptr;
        }

        int rc = ibv_req_notify_cq(cq.get(), 0);
        if (rc != 0)
        {
            LOG_ERROR_REQID("ibv_req_notify_cq failed error={},{}", *ibRequest, rc, strerror(errno));
            close(serverFd);
            return nullptr;
        }

        struct ibv_qp_init_attr qpInitAttr;
        memset(&qpInitAttr, 0, sizeof(struct ibv_qp_init_attr));

        // We use same queue for send and receive requests
        qpInitAttr.send_cq = cq.get();
        qpInitAttr.recv_cq = cq.get();
        qpInitAttr.srq = nullptr;

        // Setup reliable communication
        qpInitAttr.qp_type = IBV_QPT_RC;

        // There is only one message outstanding at any given time on the connection
        qpInitAttr.cap.max_send_wr = 1;
        qpInitAttr.cap.max_recv_wr = 1;
        qpInitAttr.cap.max_send_sge = 1;
        qpInitAttr.cap.max_recv_sge = 1;

        // Inline data limit (60 bytes) not sufficient for our use case so disabling it
        qpInitAttr.cap.max_inline_data = 0;

        // We need to track all notifications (send and receive acknowledgement)
        qpInitAttr.sq_sig_all = 1;
        unique_ibv_qp qp(ibv_create_qp(m_ibDevice.pd.get(), &qpInitAttr), FreeIbvQp);
        if (!qp)
        {
            LOG_ERROR_REQID("ibv_create_qp failed error={}", *ibRequest, strerror(errno));
            close(serverFd);
            return nullptr;
        }

        clientQpns->Add(qp->qp_num);
        vectorQps.push_back(move(qp));
        vectorCqs.push_back(move(cq));
        vectorEventChannels.push_back(move(compChannel));
    }

    // serverFd will be auto-closed by socketConnection
    unique_ptr<IConnection> socketConnection = make_unique<SocketConnection>(serverFd);
    if (!AddAuthParams(request))
    {
        LOG_WARN_REQID("failed to create auth request", *ibRequest);
        return nullptr;
    }

    if (!SendMessageToRemoteFinal(socketConnection.get(), request))
    {
        LOG_WARN_REQID("couldn't send message to server", *ibRequest);
        return nullptr;
    }

    string requestMessage;
    if (!ReadMessageFromRemote(socketConnection.get(), requestMessage))
    {
        LOG_WARN_REQID("couldn't read message from server", *ibRequest);
        return nullptr;
    }

    IBInitResponse response;
    if (!response.ParseFromString(requestMessage))
    {
        LOG_WARN_REQID("Failed to parse protobuf message", *ibRequest);
        return nullptr;
    }

    if (response.result() != IBInitResponse::SUCCESS)
    {
        LOG_WARN_REQID("unexpected response={}", *ibRequest, response.result());
        return nullptr;
    }

    if (response.serverqpn_size() != IB_CONNECTIONS_PER_SERVER)
    {
        LOG_WARN_REQID("serverqpn size mismatch {}!={}", *ibRequest, IB_CONNECTIONS_PER_SERVER, response.serverqpn_size());
        return nullptr;
    }

    // Create one to one qp mapping for client-server communication
    for (int qpn = 0; qpn < IB_CONNECTIONS_PER_SERVER; qpn++)
    {
        auto qp = move(vectorQps[qpn]);
        bool bRet = SetupQPForSendAndReceive(
            qp.get(),
            m_ibDevice.port,

            // Using minimum of server and client mtu to ensure both are configured with the same mtu value
            (ibv_mtu)std::min((uint32_t)m_ibDevice.mtu, response.servermtu()),
            response.serverqpn().Get(qpn),
            response.serverlid(),
            response.serveribport());
        if (!bRet)
        {
            LOG_WARN_REQID("Failed to setup QP id={}", *ibRequest, qp->qp_num);
            return nullptr;
        }

        // Register an MR block
        unique_ptr<char[]> mrBufferBlock(new char[IB_BUFFER_SIZE]);
        unique_ibv_mr mr(ibv_reg_mr(m_ibDevice.pd.get(), mrBufferBlock.get(), IB_BUFFER_SIZE,
            IBV_ACCESS_LOCAL_WRITE), FreeIbvMr);
        if (!mr)
        {
            LOG_WARN_REQID("ibv_reg_mr failed error={}", *ibRequest, strerror(errno));
            return nullptr;
        }

        LOG_INFO_REQID("Created IB connection, qp={}", *ibRequest, qp->qp_num);
        if (qpn < IB_CONNECTIONS_PER_SERVER - 1)
        {

            // Add all but one connections to the re-use pool
            ReleaseConnection(serverIndex, move(make_unique<IBConnection>(move(qp), move(vectorEventChannels[qpn]),
                move(vectorCqs[qpn]), move(mr), move(mrBufferBlock))), false);
        }
        else
        {
            return move(make_unique<IBConnection>(move(qp), move(vectorEventChannels[qpn]), move(vectorCqs[qpn]),
                move(mr), move(mrBufferBlock)));
        }
    }

    return nullptr;
}
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<IConnection> ConnectionManager::InitFreshConnection(int serverIndex)
{
    // TODO: Retry on failures

    struct addrinfo *structAddrInfo;
    int ret = getaddrinfo(m_vecCacheServers[serverIndex].c_str(), NULL, NULL, &structAddrInfo);
    if (ret != 0)
    {
        LOG_CRITICAL("Couldn't resolve server address={} error={}", m_vecCacheServers[serverIndex], gai_strerror(ret));
        return nullptr;
    }

    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0)
    {
        LOG_CRITICAL("Error creating a socket fd at client error={},{}", errno, strerror(errno));
        return nullptr;
    }

    sockaddr_in structSockAddr;
    structSockAddr.sin_addr = ((sockaddr_in *)structAddrInfo->ai_addr)->sin_addr;
    structSockAddr.sin_family = AF_INET;
    structSockAddr.sin_port = htons(m_ServerPort);
    freeaddrinfo(structAddrInfo);

    if (connect(serverFd, (sockaddr * ) &structSockAddr, sizeof(sockaddr)) < 0)
    {
        LOG_CRITICAL("Error during socket connect serverIndex={} error={},{}", serverIndex, errno, strerror(errno));
        StorageLibcApiWrapper::close(serverFd);
        return nullptr;
    }

    int enable = 1;
    if (setsockopt(serverFd, SOL_TCP, TCP_NODELAY, &enable, sizeof(enable)) != 0)
    {
        LOG_CRITICAL("Error setting TCP_NODELAY on client socket error={},{}", errno, strerror(errno));
        StorageLibcApiWrapper::close(serverFd);
        return nullptr;
    }

    if (m_bUseInfiniband)
    {
        return SetupIBConnections(serverFd, serverIndex);
    }

    LOG_INFO("Created new server connection fd={} serverIndex={}", serverFd, serverIndex);
    return move(make_unique<SocketConnection>(serverFd));
}
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<IConnection> ConnectionManager::GetConnection(int serverIndex)
{
    if (m_bReuseServerConnections)
    {
        // First check the connection pool for valid connections
        lock_guard queueLock(m_vecLocks[serverIndex]);
        while (!m_vecConnectionQueue[serverIndex].empty())
        {
            auto serverConnection = move(m_vecConnectionQueue[serverIndex].front());
            m_vecConnectionQueue[serverIndex].pop();
            if (serverConnection->IsValidConnection())
            {
                LOG_DEBUG("Reusing server connection id={}", serverConnection->GetId());
                return move(serverConnection);
            }
        }
    }

    return move(InitFreshConnection(serverIndex));
}
//---------------------------------------------------------------------------------------------------------------------
void ConnectionManager::ReleaseConnection(int serverIndex, std::unique_ptr<IConnection> serverConnection, bool errorFd)
{
    if (m_bReuseServerConnections && !errorFd && serverConnection->IsValidConnection())
    {
        lock_guard queueLock(m_vecLocks[serverIndex]);
        if (m_vecConnectionQueue[serverIndex].size() < m_nMaxConnsPerServer)
        {
            LOG_DEBUG("Add connection to reuse pool id={}", serverConnection->GetId());
            serverConnection->ResetConnection();
            m_vecConnectionQueue[serverIndex].push(move(serverConnection));
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------