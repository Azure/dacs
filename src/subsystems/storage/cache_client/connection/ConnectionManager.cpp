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

#include "ConnectionManager.h"
#include "CommonLogger.h"
#include "StorageLibcApiWrapper.h"
#include "SocketConnection.h"
#include "IBConnection.h"
#include "StorageMetrics.h"
#include "cache.pb.h"
#include "CacheUtils.h"
#include <errno.h>
#include <cstdlib>
#include <strings.h>
#include <fcntl.h>
#include <string>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <dlfcn.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <netdb.h>
#include <poll.h>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace cacheutils;
using namespace ibutils;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
 /**
 * Threading Overview in ConnectionManager:
 * 
 * The `ConnectionManager` class uses threading to manage cache server discovery and connection pooling efficiently.
 * Below are the threading-related components and their roles:
 * 
 * 1. **Mutexes and Locks**:
 *    - `m_lockCacheServerConnections`: Protects access to `m_mapConnectionQueue` to ensure thread-safe
 *      operations when updating or accessing cache server connections.
 *    - `m_sharedLockCacheServerData`: A shared mutex that allows multiple threads to read the cache server data
 *      concurrently while ensuring exclusive access during updates.
 *    - `m_lockIBHandshakeStatusMap`: Protects access to `m_mapIbHandshakeStatus` to ensure thread-safe updates
 *      to Infiniband handshake statuses.
 * 
 * 2. **Background Thread**:
 *    - `m_cacheServerRefresherThread`: A dedicated thread that periodically fetches the latest cache server list
 *      from the discovery endpoint and updates the internal cache server list.
 *    - This thread runs in the `MonitorCacheServers` method and sleeps for `m_nCacheServerDiscoveryIntervalInSeconds`
 *      between updates.
 *    - The background thread (`m_cacheServerRefresherThread`) is started in the constructor if cache server discovery
 *      is enabled (`m_bEnableCacheServerDiscovery`).
 *    - The thread is gracefully stopped in the destructor.
 * 
 * 3. **Thread-Safe Operations**:
 *    - Shared data structures such as `m_vecCacheServers`, `m_mapConnectionQueue`, and
 *      `m_mapIbHandshakeStatus` are accessed or modified within critical sections protected by appropriate locks:
 *        - `std::lock_guard<std::mutex>` for exclusive access.
 *        - `std::shared_lock<std::shared_mutex>` for concurrent read access.
 *        - `std::unique_lock<std::shared_mutex>` for exclusive write access.
 * 
 * 4. **RAII Principles**:
 *    - Mutexes are managed using RAII constructs such as `std::lock_guard` and `std::unique_lock` to ensure they are
 *      automatically released when the scope ends. This prevents deadlocks and ensures safe access to shared resources.
 * 
 * 5. **Infiniband Handshake Management**:
 *    - The `m_lockIBHandshakeStatusMap` mutex ensures thread-safe updates to the Infiniband handshake status map
 *      (`m_mapIbHandshakeStatus`).
 *    - Handshake operations are performed in `PerformIBHandshake` and tracked using `m_mapIbHandshakeStatus`.
 * 
 * 6. **Cache Server Updates**:
 *    - The `UpdateCacheServers` method is responsible for updating the cache server list. It uses a combination of
 *      `std::lock_guard` and `std::unique_lock` to ensure thread-safe modifications to
 *      `m_vecCacheServers`, and `m_mapConnectionQueue`.
 * 
 * 7. **Connection Pooling**:
 *    - The `m_mapConnectionQueue` maintains a pool of reusable connections for each cache server.
 *    - Connections are added to the pool in `ReleaseConnection` and retrieved from the pool in `GetConnection`.
 *    - The `m_lockCacheServerConnections` mutex ensures thread-safe access to the connection pool.
 * 
 * 8. **Graceful Shutdown**:
 *    - The destructor ensures that all resources are cleaned up properly:
 *        - All open connections in `m_mapConnectionQueue` are purged.
 *        - The Infiniband device resources (`m_ibDevice`) are released.
 *        - The background thread (`m_cacheServerRefresherThread`) is joined to ensure it has stopped.
 */
ConnectionManager::ConnectionManager(
    /*[in]*/ const uint16_t serverPort,
    /*[in]*/ const bool useLocalServer,
    /*[in]*/ const std::vector<std::string>& vecCacheServers,
    /*[in]*/ const uint32_t nMaxConnectionsReuseSize,
    /*[in]*/ const bool bReuseServerConnections,
    /*[in]*/ const bool bUseInfiniband,
    /*[in]*/ const bool bEnableCacheServerDiscovery,
    /*[in]*/ const uint32_t nCacheServerDiscoveryIntervalInSeconds,
    /*[in]*/ const std::string& cacheDiscoveryEndpoint,
    /*[in]*/ const bool bEnableConsistentHashing,
    /*[in]*/ const int nSocketSndbufSize,
    /*[in]*/ const int nSocketRcvbufSize)
    :  m_ServerPort(serverPort)
    ,  m_nMaxConnectionsReuseSize(nMaxConnectionsReuseSize)
    ,  m_nMaxConnsPerServer(0)
    ,  m_bReuseServerConnections(bReuseServerConnections)
    ,  m_bUseInfiniband(bUseInfiniband)
    ,  m_bEnableCacheServerDiscovery(bEnableCacheServerDiscovery)
    ,  m_nCacheServerDiscoveryIntervalInSeconds(nCacheServerDiscoveryIntervalInSeconds)
    ,  m_cacheDiscoveryEndpoint(cacheDiscoveryEndpoint)
    ,  m_nSocketSndbufSize(nSocketSndbufSize)
    ,  m_nSocketRcvbufSize(nSocketRcvbufSize)
    ,  m_bIbZeroCopyEnabled(IsIbZeroCopyEnabled())
    ,  m_hashingStrategy(bEnableConsistentHashing 
        ? HashingStrategy::ConsistentHashing
        : HashingStrategy::StaticModuloHashing)
{
    m_ibDevice = {0};
    m_hasher = HashingFactory::CreateHasher(m_hashingStrategy);
    if (!m_hasher) {
        LOG_ERROR("Failed to create hasher for strategy: {}", 
                static_cast<int>(m_hashingStrategy));
        throw std::runtime_error("Failed to initialize hashing strategy");
    }

    if (useLocalServer)
    {
        // Infiniband is not necessary when using localhost
        m_bUseInfiniband = false;
        std::string localServer = "127.0.0.1";
        LOG_INFO("Using {} as cache server", localServer);
        UpdateCacheServers({localServer});
    }
    else
    {   // Using remote servers
        if (m_bEnableCacheServerDiscovery)
        {
            LOG_INFO("Cache server discovery is enabled");
            auto vecServersPtr = FetchLatestCacheServers();
            if (!vecServersPtr) {
                LOG_WARN("Failed to fetch the cache servers from endpoint: {}. Cache client will start with empty "
                    "server list and rely on background discovery.", m_cacheDiscoveryEndpoint);
            }
            else
            {
                UpdateCacheServers(*vecServersPtr);
            }
        }
        else
        {
            LOG_INFO("Cache server discovery is not enabled");
            if (vecCacheServers.empty())
            {
                LOG_CRITICAL("No cache servers configured");
                throw "No cache servers configured";
            }
            UpdateCacheServers(vecCacheServers);
        }
    }
    m_hasher->UpdateServers(m_vecCacheServers);

    if (m_bUseInfiniband)
    {
        InitInfiniband();
    }

    LOG_INFO("Initialized Cache ConnectionManager. serverPort={} "
        "m_nMaxConnsPerServer={} m_bReuseServerConnections={} m_bUseInfiniband={} "
        "m_bEnableCacheServerDiscovery={} m_nCacheServerDiscoveryIntervalInSeconds={} "
        "m_cacheDiscoveryEndpoint={} HashingStrategy={}",
        serverPort,
        m_nMaxConnsPerServer,
        m_bReuseServerConnections,
        m_bUseInfiniband,
        m_bEnableCacheServerDiscovery,
        m_nCacheServerDiscoveryIntervalInSeconds,
        m_cacheDiscoveryEndpoint,
        m_hashingStrategy == HashingStrategy::ConsistentHashing ? "ConsistentHashing" : "StaticModuloHashing"
    );
    
    if (m_bEnableCacheServerDiscovery) {
        this->m_cacheServerRefresherThread = std::thread(&ConnectionManager::MonitorCacheServers, this);
        LOG_INFO("Background cache server discovery thread is enabled");
    }
}
//---------------------------------------------------------------------------------------------------------------------
ConnectionManager::~ConnectionManager()
{
    LOG_INFO("Purge all open connections");
    {
        std::lock_guard<std::mutex> lock(m_lockCacheServerConnections);
        for (auto &pair : m_mapConnectionQueue)
        {
            auto &queueFd = pair.second;
            while (!queueFd.empty())
            {
                auto serverConnection = std::move(queueFd.front());
                queueFd.pop();
            }
        }
    }

    m_ibDevice.pd.reset(nullptr);
    m_ibDevice.context.reset(nullptr);

    m_bStopCacheServerRefresher.store(true);
    m_cvStopCacheServerRefresher.notify_all();
    if (m_cacheServerRefresherThread.joinable()) {
        m_cacheServerRefresherThread.join();
        LOG_INFO("Cache server discovery thread joined");
    }
}
//---------------------------------------------------------------------------------------------------------------------
/** 
* The member variables m_vecCacheServers, m_nMaxConnsPerServer
* are only mutated by this method.
* This method is an private method and is called only from the constructor and MonitorCacheServers.
* This method is never called from multiple threads at the same time.
*/
void ConnectionManager::UpdateCacheServers(const std::vector<std::string>& vecLatestCacheServers)
{
    // Locking the cache server objects to ensure thread safety
    std::lock_guard<std::mutex> lockConnections(m_lockCacheServerConnections);
    std::unique_lock<std::shared_mutex> lockData(m_sharedLockCacheServerData); // Exclusive lock for write access

    int nOldCacheServers = m_vecCacheServers.size();
    int nNewCacheServers = vecLatestCacheServers.size();

    // Check if the cache server list is changed. Using a Set<string> to eliminate duplicate hostnames (if any).
    std::set<std::string> oldCacheServersSet(m_vecCacheServers.begin(), m_vecCacheServers.end());
    std::set<std::string> newCacheServersSet(vecLatestCacheServers.begin(), vecLatestCacheServers.end());
    if (oldCacheServersSet == newCacheServersSet)
    {
        LOG_DEBUG("Cache servers have not changed.");
        return;
    }

    // Cache server set has changed. Begin updating.
    LOG_INFO("Updating cache server list. Old set: {}, New set: {}",
        fmt::join(oldCacheServersSet, ", "),
        fmt::join(newCacheServersSet, ", "));

    // Finding deleted cache servers and updating the map accordingly
    std::vector<std::string> eliminatedCacheServers;
    std::set_difference(oldCacheServersSet.begin(), oldCacheServersSet.end(),
                        newCacheServersSet.begin(), newCacheServersSet.end(),
                        std::back_inserter(eliminatedCacheServers));

    // Finding newly added cache servers and updating the map accordingly
    std::vector<std::string> newlyAddedCacheServers;
    std::set_difference(newCacheServersSet.begin(), newCacheServersSet.end(),
                        oldCacheServersSet.begin(), oldCacheServersSet.end(),
                        std::back_inserter(newlyAddedCacheServers));

    LOG_INFO("Eliminated cache servers: {}", fmt::join(eliminatedCacheServers, ", "));
    for (const auto& server : eliminatedCacheServers)
    {
        if (m_mapConnectionQueue.find(server) != m_mapConnectionQueue.end())
        {
            m_mapConnectionQueue.erase(server);
        }
    }

    LOG_INFO("Newly added cache servers: {}", fmt::join(newlyAddedCacheServers, ", "));
    for (const auto& server : newlyAddedCacheServers)
    {
        if (m_mapConnectionQueue.find(server) == m_mapConnectionQueue.end())
        {
            m_mapConnectionQueue[server] = std::queue<std::unique_ptr<IConnection>>();
        }
    }

    m_vecCacheServers.assign(newCacheServersSet.begin(), newCacheServersSet.end());
    uint32_t nServerNodes = m_vecCacheServers.size();
    m_nMaxConnsPerServer = (m_nMaxConnectionsReuseSize + nServerNodes - 1) / nServerNodes;

    m_hasher->UpdateServers(m_vecCacheServers);
    LOG_DEBUG("Updated hasher with {} servers", m_vecCacheServers.size());

    LOG_INFO("New list of cache servers: {}, nServerNodes={}", fmt::join(m_vecCacheServers, ", "), nServerNodes);
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
    m_ibDevice.context = std::move(context);
    m_ibDevice.pd = std::move(pd);

    // Query device capabilities to determine max SGE support
    struct ibv_device_attr devAttr{};
    int rc = ibv_query_device(m_ibDevice.context.get(), &devAttr);
    if (rc != 0)
    {
        LOG_WARN("ibv_query_device failed error={},{}, defaulting max_sge=1", rc, strerror(errno));
        m_ibDevice.max_sge = 1;
    }
    else
    {
        m_ibDevice.max_sge = devAttr.max_sge;
        LOG_INFO("IB device max_sge={}", m_ibDevice.max_sge);
    }

    m_bUseInfiniband = true;
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<IConnection> ConnectionManager::SetupIBConnections(std::unique_ptr<IConnection> socketConnection, const std::string& cacheserver)
{
    Request request;
    IBInitRequest* ibRequest = request.mutable_ibinitrequest();
    ibRequest->set_requestid(cacheutils::GetCorrelationId());
    ibRequest->set_clientibport(m_ibDevice.port);
    ibRequest->set_clientlid(m_ibDevice.lid);
    ibRequest->set_clientmtu(m_ibDevice.mtu);
    auto clientQpns = ibRequest->mutable_clientqpn();
    clientQpns->Clear();
    
    SocketConnection* socketConn = static_cast<SocketConnection*>(socketConnection.get());
    int serverFd = socketConn->GetId();
    LOG_DEBUG_REQID("SetupIBConnections: serverFd={} cacheserver={}", *ibRequest, serverFd, cacheserver);

    vector<unique_ibv_comp_channel> vectorEventChannels;
    vector<unique_ibv_cq> vectorCqs;
    vector<unique_ibv_qp> vectorQps;
    for (int qpn = 0; qpn < IB_CONNECTIONS_PER_SERVER; qpn++)
    {
        unique_ibv_comp_channel compChannel(ibv_create_comp_channel(m_ibDevice.context.get()), FreeIbvCompChannel);
        if (!compChannel)
        {
            LOG_ERROR_REQID("ibv_create_comp_channel failed error={}", *ibRequest, strerror(errno));
            std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
            m_mapIbSetupStatus[cacheserver] = IBSetupStatus::PROTOCOL_ERROR;
            return socketConnection;
        }

        unique_ibv_cq cq(ibv_create_cq(m_ibDevice.context.get(), 2, NULL, compChannel.get(), 0), FreeIbvCq);
        if (!cq)
        {
            LOG_ERROR_REQID("ibv_create_cq failed error={}", *ibRequest, strerror(errno));
            std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
            m_mapIbSetupStatus[cacheserver] = IBSetupStatus::PROTOCOL_ERROR;
            return socketConnection;
        }

        int rc = ibv_req_notify_cq(cq.get(), 0);
        if (rc != 0)
        {
            LOG_ERROR_REQID("ibv_req_notify_cq failed error={},{}", *ibRequest, rc, strerror(errno));
            std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
            m_mapIbSetupStatus[cacheserver] = IBSetupStatus::PROTOCOL_ERROR;
            return socketConnection;
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
        // 2 SGEs needed for uploads: SGE[0]=protobuf header from MR buffer,
        // SGE[1]=data from caller's directly-registered buffer.
        // Downloads and other ops use only 1 SGE but the limit applies per-QP.
        // Cap to device max_sge to avoid ibv_create_qp failure on limited hardware.
        int requestedSge = std::min(2, m_ibDevice.max_sge);
        qpInitAttr.cap.max_send_sge = requestedSge;
        qpInitAttr.cap.max_recv_sge = 1;

        // Inline data limit (60 bytes) not sufficient for our use case so disabling it
        qpInitAttr.cap.max_inline_data = 0;

        // We need to track all notifications (send and receive acknowledgement)
        qpInitAttr.sq_sig_all = 1;
        unique_ibv_qp qp(ibv_create_qp(m_ibDevice.pd.get(), &qpInitAttr), FreeIbvQp);
        if (!qp)
        {
            LOG_ERROR_REQID("ibv_create_qp failed error={}", *ibRequest, strerror(errno));
            std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
            m_mapIbSetupStatus[cacheserver] = IBSetupStatus::PROTOCOL_ERROR;
            return socketConnection;
        }

        clientQpns->Add(qp->qp_num);
        vectorQps.push_back(std::move(qp));
        vectorCqs.push_back(std::move(cq));
        vectorEventChannels.push_back(std::move(compChannel));
    }

    // serverFd will be auto-closed by socketConnection

    if (!SendMessageToRemoteFinal(socketConnection.get(), request))
    {
        LOG_WARN_REQID("couldn't send message to server", *ibRequest);
        std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
        m_mapIbSetupStatus[cacheserver] = IBSetupStatus::NETWORK_ERROR;
        return nullptr; // not returning socket connection for network failures
    }

    std::string requestMessage;
    if (!ReadMessageFromRemote(socketConnection.get(), requestMessage))
    {
        LOG_WARN_REQID("couldn't read message from server", *ibRequest);
        std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
        m_mapIbSetupStatus[cacheserver] = IBSetupStatus::NETWORK_ERROR;
        return nullptr; // not returning socket connection for network failures
    }

    // reset socket connection as it has been used to send the IB init request
    socketConnection->ResetConnection();
    IBInitResponse response;
    if (!response.ParseFromString(requestMessage))
    {
        LOG_WARN_REQID("Failed to parse protobuf message", *ibRequest);
        std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
        m_mapIbSetupStatus[cacheserver] = IBSetupStatus::FAILURE;
        return socketConnection;
    }

    if (response.requestid().empty())
    {
        LOG_DEBUG_REQID(
            "IB init response request ID is unavailable; accepting for legacy compatibility: server={}",
            *ibRequest,
            cacheserver);
    }
    else if (response.requestid() != ibRequest->requestid())
    {
        LOG_ERROR_REQID(
            "IB init response request ID mismatch: server={}, requestedId={}, responseId={}",
            *ibRequest,
            cacheserver,
            ibRequest->requestid(),
            response.requestid());
        std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
        m_mapIbSetupStatus[cacheserver] = IBSetupStatus::FAILURE;
        return nullptr;
    }

    if (response.result() != IBInitResponse::SUCCESS)
    {
        if (response.result() == IBInitResponse::IB_NOT_SUPPORTED)
        {
            LOG_INFO_REQID("server does not support IB connections", *ibRequest);
        }
        else
        {
            LOG_WARN_REQID("unexpected response={}", *ibRequest, response.result());
        }
        std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
        m_mapIbSetupStatus[cacheserver] = IBSetupStatus::FAILURE;
        return socketConnection;
    }

    if (response.serverqpn_size() != IB_CONNECTIONS_PER_SERVER)
    {
        LOG_WARN_REQID("serverqpn size mismatch {}!={}", *ibRequest, IB_CONNECTIONS_PER_SERVER, response.serverqpn_size());
        std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
        m_mapIbSetupStatus[cacheserver] = IBSetupStatus::FAILURE;
        return socketConnection;
    }

    // Create one to one qp mapping for client-server communication
    for (int qpn = 0; qpn < IB_CONNECTIONS_PER_SERVER; qpn++)
    {
        auto qp = std::move(vectorQps[qpn]);
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
            std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
            m_mapIbSetupStatus[cacheserver] = IBSetupStatus::PROTOCOL_ERROR;
            return socketConnection;
        }

        // Register an MR block
        unique_ptr<char[]> mrBufferBlock(new char[IB_BUFFER_SIZE]);
        unique_ibv_mr mr(ibv_reg_mr(m_ibDevice.pd.get(), mrBufferBlock.get(), IB_BUFFER_SIZE,
            static_cast<int>(IBV_ACCESS_LOCAL_WRITE)), FreeIbvMr);
        if (!mr)
        {
            LOG_WARN_REQID("ibv_reg_mr failed error={}", *ibRequest, strerror(errno));
            std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
            m_mapIbSetupStatus[cacheserver] = IBSetupStatus::PROTOCOL_ERROR;
            return socketConnection;
        }

        LOG_DEBUG_REQID("Created IB connection, qp={}", *ibRequest, qp->qp_num);
        // Use the zero-copy path only if it's enabled and the device supports 2 SGEs.
        bool directMrEnabled = (m_ibDevice.max_sge >= 2) && m_bIbZeroCopyEnabled;
        std::unique_ptr<IBConnection> ibConnection = make_unique<IBConnection>(
            std::move(qp), 
            std::move(vectorEventChannels[qpn]),
            std::move(vectorCqs[qpn]), 
            std::move(mr), 
            std::move(mrBufferBlock),
            directMrEnabled
        );
        if (!PerformIBHandshake(ibConnection.get(), cacheserver))
        {
            std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
            m_mapIbSetupStatus[cacheserver] = IBSetupStatus::FAILURE;
            LOG_WARN_REQID("IB handshake failed for server={}", *ibRequest, cacheserver);
            return socketConnection;
        }
        if (qpn < IB_CONNECTIONS_PER_SERVER - 1)
        {
            // Add all but one connections to the re-use pool
            ReleaseConnection(cacheserver, std::move(ibConnection), false);
        }
        else
        {
            std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
            m_mapIbSetupStatus[cacheserver] = IBSetupStatus::SUCCESS;
            return std::move(ibConnection);           
        }
    }

    std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
    m_mapIbSetupStatus[cacheserver] = IBSetupStatus::FAILURE;
    return socketConnection;
}
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<IConnection> ConnectionManager::CreateSocketAndConnect(const std::string& hostname, uint16_t port) const
{
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int ret = getaddrinfo(hostname.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (ret != 0)
    {
        LOG_WARN("Failed to resolve server address={} error={}", hostname, gai_strerror(ret));
        STORAGE_METRIC_COUNTER_INCREMENT("cache_client_dns_errors", 1, {{"hostname", hostname}});
        return nullptr;
    }

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0)
    {
        LOG_WARN("Failed to create socket for hostname={} error={}", hostname, strerror(errno));
        freeaddrinfo(res);
        return nullptr;
    }

    // Default socket timeout can be very high (upto 2 minutes) which can cause a connection error to significantly slow download\upload.
    // Setting it to 5 seconds by using non-blocking connect with select. TODO(adbansal): make this configurable.
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0)
    {
        LOG_WARN("Failed to get socket flags for hostname={} error={}", hostname, strerror(errno));
        close(sockfd);
        freeaddrinfo(res);
        return nullptr;
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        LOG_WARN("Failed to set socket to non-blocking for hostname={} error={}", hostname, strerror(errno));
        close(sockfd);
        freeaddrinfo(res);
        return nullptr;
    }

    int connectRet;
    uint32_t connectAttempts = 0;
    do {
        connectRet = connect(sockfd, res->ai_addr, res->ai_addrlen);
        connectAttempts++;
    } while (connectRet < 0 && errno == EINTR && connectAttempts < 3);

    if (connectRet < 0)
    {
        if (errno != EINPROGRESS && errno != EALREADY)
        {
            LOG_WARN("Failed to initiate connection to hostname={} attempts={} errno={} error={}",
                hostname, connectAttempts, errno, strerror(errno));
            close(sockfd);
            freeaddrinfo(res);
            return nullptr;
        }

        if (!WaitForSocketWritable(sockfd, 5000))
        {
            LOG_WARN("Connection timed out or socket not writable for hostname={}", hostname);
            close(sockfd);
            freeaddrinfo(res);
            return nullptr;
        }

        int so_error;
        socklen_t len = sizeof(so_error);
        if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0)
        {
            LOG_WARN("Connection failed after select for hostname={} errno={} error={}",
                hostname, so_error, strerror(so_error));
            close(sockfd);
            freeaddrinfo(res);
            return nullptr;
        }
    }

    // Restore socket to blocking mode
    if (fcntl(sockfd, F_SETFL, flags) < 0)
    {
        LOG_WARN("Failed to restore socket to blocking mode for hostname={} error={}", hostname, strerror(errno));
        close(sockfd);
        freeaddrinfo(res);
        return nullptr;
    }

    freeaddrinfo(res);

    int enable = 1;
    if (setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &enable, sizeof(enable)) != 0)
    {
        LOG_WARN("Failed to set TCP_NODELAY for hostname={} error={}", hostname, strerror(errno));
        close(sockfd);
        return nullptr;
    }

    if (m_nSocketSndbufSize > 0)
    {
        if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &m_nSocketSndbufSize, sizeof(m_nSocketSndbufSize)) != 0)
        {
            LOG_WARN("Failed to set SO_SNDBUF={} for hostname={} error={}", m_nSocketSndbufSize, hostname, strerror(errno));
        }
    }
    if (m_nSocketRcvbufSize > 0)
    {
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &m_nSocketRcvbufSize, sizeof(m_nSocketRcvbufSize)) != 0)
        {
            LOG_WARN("Failed to set SO_RCVBUF={} for hostname={} error={}", m_nSocketRcvbufSize, hostname, strerror(errno));
        }
    }

    if (m_nSocketSndbufSize > 0 || m_nSocketRcvbufSize > 0)
    {
        int actualSndbuf = 0, actualRcvbuf = 0;
        socklen_t optlen = sizeof(actualSndbuf);
        if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &actualSndbuf, &optlen) == 0 &&
            getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &actualRcvbuf, &optlen) == 0)
        {
            LOG_INFO("Socket buffer sizes for hostname={}: SO_SNDBUF requested={} actual={}, SO_RCVBUF requested={} actual={}",
                hostname, m_nSocketSndbufSize, actualSndbuf, m_nSocketRcvbufSize, actualRcvbuf);
        }
    }

    LOG_DEBUG("Successfully connected to hostname={} on port={}", hostname, port);
    return std::make_unique<SocketConnection>(sockfd);
}
//---------------------------------------------------------------------------------------------------------------------
// Uses poll() so it is safe for fd numbers >= FD_SETSIZE.
// The old select()+FD_SET/FD_ISSET approach had undefined behaviour for fd >= FD_SETSIZE (1024):
// FD_SET silently writes past the end of the fd_set bitmap, and FD_ISSET subsequently returns
// false, causing the connection attempt to be incorrectly treated as failed.
bool ConnectionManager::WaitForSocketWritable(int sockfd, int timeoutMs)
{
    struct pollfd pfd;
    int pollRet;
    uint32_t attempts = 0;
    do {
        pfd.fd = sockfd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        pollRet = poll(&pfd, 1, timeoutMs);
        attempts++;
    } while (pollRet < 0 && errno == EINTR && attempts < 3);

    if (pollRet == 0)
    {
        LOG_WARN("poll() timed out for fd={} attempts={}", sockfd, attempts);
        return false;
    }
    if (pollRet < 0)
    {
        LOG_WARN("poll() failed for fd={} attempts={} errno={} error={}",
            sockfd, attempts, errno, strerror(errno));
        return false;
    }
    if (!(pfd.revents & POLLOUT) || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
    {
        LOG_WARN("Socket fd={} not writable or in error state after poll(), revents={}", sockfd, pfd.revents);
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool ConnectionManager::IsIbZeroCopyEnabled()
{
    // disabled by default. Set DACS_ENABLE_IB_ZERO_COPY=true to use the new zero-copy IB upload path.
    char* env = std::getenv("DACS_ENABLE_IB_ZERO_COPY");
    return env != NULL && strcasecmp(env, "true") == 0;
}
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<IConnection> ConnectionManager::InitFreshConnection(const std::string& cacheserver)
{
    std::unique_ptr<IConnection> socketConnection = CreateSocketAndConnect(cacheserver, m_ServerPort);
    if (!socketConnection)
    {
        LOG_WARN("Failed to initialize fresh connection to cacheserver={}", cacheserver);
        return nullptr;
    }

    SocketConnection* socketConn = static_cast<SocketConnection*>(socketConnection.get());
    if (m_bUseInfiniband)
    {
        IBSetupStatus setupStatus;
        {
            std::lock_guard<std::mutex> lock(m_lockIbSetupStatusMap);
            auto it = m_mapIbSetupStatus.find(cacheserver);
            setupStatus = (it != m_mapIbSetupStatus.end()) ? it->second : IBSetupStatus::NOT_ATTEMPTED;
        }

        LOG_DEBUG("IB setup status for server={} is {}", cacheserver, ToStringIBSetupStatus(setupStatus));
        // If IB setup was previously attempted and failed, skip re-attempting
        if (setupStatus == IBSetupStatus::FAILURE) {
            LOG_INFO("Skipping IB setup for server={} due to previous status={} "
                "falling back to socket connection", cacheserver, ToStringIBSetupStatus(setupStatus));
            return std::move(socketConnection);
        }

        // If IB setup was previously attempted and succeeded or failed due to network or protocol error, re-attempt
        return SetupIBConnections(std::move(socketConnection), cacheserver);
    }

    LOG_DEBUG("Created new server connection fd={} cacheserver={}", socketConn->GetId(), cacheserver);
    return std::move(socketConnection);
}
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<IConnection> ConnectionManager::GetConnection(const std::string& cacheserver)
{
    // Handle empty server name (when no servers are discovered yet)
    if (cacheserver.empty())
    {
        LOG_DEBUG("Cannot create connection to empty server name. Cache servers may not be discovered yet.");
        return nullptr;
    }

    if (m_bReuseServerConnections)
    {
        // First check the connection pool for valid connections
        std::lock_guard<std::mutex> lock(m_lockCacheServerConnections);
        if (std::find(m_vecCacheServers.begin(), m_vecCacheServers.end(), cacheserver) == m_vecCacheServers.end())
        {
            LOG_INFO("Invalid cacheserver={} to perform get connection", cacheserver);
            return nullptr;
        }
        while (!m_mapConnectionQueue[cacheserver].empty())
        {
            auto serverConnection = std::move(m_mapConnectionQueue[cacheserver].front());
            m_mapConnectionQueue[cacheserver].pop();
            if (serverConnection->IsValidConnection())
            {
                LOG_DEBUG("Reusing server connection id={}", serverConnection->GetId());
                return std::move(serverConnection);
            }
        }
    }

    return std::move(InitFreshConnection(cacheserver));
}
//---------------------------------------------------------------------------------------------------------------------
void ConnectionManager::ReleaseConnection(
    const std::string& cacheserver, 
    std::unique_ptr<IConnection> serverConnection,
    const bool errorFd)
{
    if (m_bReuseServerConnections && !errorFd && serverConnection->IsValidConnection())
    {
        std::lock_guard<std::mutex> lock(m_lockCacheServerConnections);
        if (std::find(m_vecCacheServers.begin(), m_vecCacheServers.end(), cacheserver) == m_vecCacheServers.end())
        {
            LOG_DEBUG("Invalid cacheserver={} to perform release connection", cacheserver);
            return;
        }
        if (m_mapConnectionQueue[cacheserver].size() < m_nMaxConnsPerServer)
        {
            LOG_DEBUG("Add connection to reuse pool id={}", serverConnection->GetId());
            serverConnection->ResetConnection();
            m_mapConnectionQueue[cacheserver].push(std::move(serverConnection));
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool ConnectionManager::PerformIBHandshake(IConnection* connection, const std::string& cacheserver)
{
    {
        std::lock_guard<std::mutex> lock(m_lockIBHandshakeStatusMap);
        // check if handshake is already performed
        auto mapIt = m_mapIbHandshakeStatus.find(cacheserver);
        if (mapIt != m_mapIbHandshakeStatus.end() && mapIt->second == HandshakeStatus::SUCCESS)
        {
            LOG_DEBUG("IB handshake already successfully performed for qp={}, cacheserver={}", 
                connection->GetId(), cacheserver);
            return true;
        }
        else if (mapIt != m_mapIbHandshakeStatus.end() && mapIt->second == HandshakeStatus::FAILURE)
        {
            LOG_DEBUG("Previous IB handshake failed for qp={}, cacheserver={}",
                connection->GetId(), cacheserver);
            return false;
        }
    }

    HandshakeStatus newHandShakeStatus = NewIBHandshake(connection, cacheserver);
    std::lock_guard<std::mutex> lock(m_lockIBHandshakeStatusMap);
    if (newHandShakeStatus == HandshakeStatus::FAILURE)
    {
        m_mapIbHandshakeStatus[cacheserver] = HandshakeStatus::FAILURE;
        connection->ResetConnection();
        return false;
    }

    LOG_DEBUG("IB handshake successful qp={}, cacheserver={}", connection->GetId(), cacheserver);
    m_mapIbHandshakeStatus[cacheserver] = HandshakeStatus::SUCCESS;
    connection->ResetConnection();
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
ConnectionManager::HandshakeStatus ConnectionManager::NewIBHandshake(
    IConnection* connection, const std::string& cacheserver) const
{
    LOG_DEBUG("Performing IB handshake qp={}, cacheserver={}", connection->GetId(), cacheserver);
    // Create an download request message for a file
    Request request;
    DownloadRequest* downloadRequest = request.mutable_downloadrequest();
    std::string strRandId = cacheutils::GetCorrelationId();
    const char* strCorrelationId = strRandId.c_str();
    std::size_t nReadLength = 512;
    downloadRequest->set_requestid(strCorrelationId);
    downloadRequest->set_filename("IB_HANDSHAKE_TEST_FILE");
    downloadRequest->set_offset(0);
    downloadRequest->set_length(nReadLength);
    downloadRequest->set_enablelock(false);

    // Send the request message to the server
    bool bRet = SendMessageToRemoteFinal(connection, request);
    if (!bRet)
    {
        LOG_WARN("IB handshake failed, Failed to send request message qp={}", connection->GetId());
        return HandshakeStatus::FAILURE;
    }

    // Wait for the "not found" response
    std::string responseBuffer;
    if (!ReadMessageFromRemote(connection, responseBuffer))
    {
        LOG_WARN("IB handshake failed, Failed to receive response qp={}", connection->GetId());
        return HandshakeStatus::FAILURE;
    }

    // Parse the response
    DownloadResponse response;
    if (!response.ParseFromString(responseBuffer))
    {
        LOG_WARN("IB handshake failed, Failed to parse response qp={}", connection->GetId());
        return HandshakeStatus::FAILURE;
    }

    if (response.requestid().empty())
    {
        LOG_DEBUG_REQID(
            "IB handshake response request ID is unavailable; accepting for legacy compatibility: server={}",
            *downloadRequest,
            cacheserver);
    }
    else if (response.requestid() != downloadRequest->requestid())
    {
        LOG_ERROR_REQID(
            "IB handshake response request ID mismatch: server={}, requestedId={}, responseId={}",
            *downloadRequest,
            cacheserver,
            downloadRequest->requestid(),
            response.requestid());
        return HandshakeStatus::FAILURE;
    }

    if (response.result() != DownloadResponse::NOT_FOUND)
    {
        LOG_WARN("IB handshake failed, Invalid response received qp={}", connection->GetId());
        return HandshakeStatus::FAILURE;
    }

    return HandshakeStatus::SUCCESS;
}
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<std::vector<std::string>> ConnectionManager::FetchLatestCacheServers() const
{
    Request request;
    GetCacheServersRequest* getCacheServersRequest = request.mutable_getcacheserversrequest();
    getCacheServersRequest->set_requestid(cacheutils::GetCorrelationId());

    std::unique_ptr<IConnection> serverConnection = CreateSocketAndConnect(m_cacheDiscoveryEndpoint, m_ServerPort);
    if (!serverConnection)
    {
        LOG_WARN("Couldn't create server connection for fetching cache servers");
        return nullptr;
    }

    LOG_DEBUG("Sending request to fetch cache servers");
    if (!SendMessageToRemoteFinal(serverConnection.get(), request))
    {
        LOG_WARN("Couldn't send message to server for fetching cache servers");
        return nullptr;
    }

    std::string requestMessage;
    if (!ReadMessageFromRemote(serverConnection.get(), requestMessage))
    {
        LOG_WARN("Couldn't read message from server for fetching cache servers");
        return nullptr;
    }

    GetCacheServersResponse response;
    if (!response.ParseFromString(requestMessage))
    {
        LOG_WARN("Failed to parse response from server for fetching cache servers");
        return nullptr;
    }

    if (response.requestid().empty())
    {
        LOG_DEBUG_REQID(
            "Cache discovery response request ID is unavailable; accepting for legacy compatibility",
            *getCacheServersRequest);
    }
    else if (response.requestid() != getCacheServersRequest->requestid())
    {
        LOG_ERROR_REQID(
            "Cache discovery response request ID mismatch: requestedId={}, responseId={}",
            *getCacheServersRequest,
            getCacheServersRequest->requestid(),
            response.requestid());
        return nullptr;
    }

    if (response.result() != GetCacheServersResponse::SUCCESS)
    {
        LOG_WARN("Server returned error for fetching cache servers - {}", response.DebugString());
        return nullptr;
    }

    auto vecServers = std::make_unique<std::vector<std::string>>();
    for (const auto& serverAddress : response.serveraddresses())
    {
        vecServers->push_back(serverAddress);
    }
    LOG_DEBUG("Discovery api response cache servers - {}", *vecServers);
    return vecServers;
}
//---------------------------------------------------------------------------------------------------------------------
void ConnectionManager::MonitorCacheServers()
{
    while (!m_bStopCacheServerRefresher.load())
    {
        // Wait for the discovery interval or until signaled to stop.
        {
            std::unique_lock<std::mutex> lock(m_mtxStopCacheServerRefresher);
            m_cvStopCacheServerRefresher.wait_for(lock,
                std::chrono::seconds(m_nCacheServerDiscoveryIntervalInSeconds),
                [this] { return m_bStopCacheServerRefresher.load(); });
        }
        if (m_bStopCacheServerRefresher.load())
            break;

        // Fetch the latest cache servers
        auto vecServersPtr = FetchLatestCacheServers();
        if (!vecServersPtr) {
            LOG_WARN("Failed to fetch the latest cache servers. Skipping update.");
            continue;
        }
        UpdateCacheServers(*vecServersPtr);
    }
}
//---------------------------------------------------------------------------------------------------------------------
std::string ConnectionManager::GetCacheServerHostname(const std::string& strCacheKey) const
{
    std::shared_lock<std::shared_mutex> readLock(m_sharedLockCacheServerData); // Shared lock for concurrent read access
    
    // Handle case where no servers are available yet during discovery
    if (m_vecCacheServers.empty())
    {
        LOG_DEBUG("No cache servers available for key: {}. Discovery may still be in progress.", strCacheKey);
        return "";  // Return empty string to indicate no server available
    }

    return m_hasher->GetServer(strCacheKey);
}
//---------------------------------------------------------------------------------------------------------------------
std::vector<std::string> ConnectionManager::GetAllCacheServerHostnames() const
{
    std::shared_lock<std::shared_mutex> readLock(m_sharedLockCacheServerData); // Shared lock for concurrent read access
    return m_vecCacheServers;
}
//---------------------------------------------------------------------------------------------------------------------
const char* ConnectionManager::ToStringIBSetupStatus(IBSetupStatus status)
{
    switch (status)
    {
        case IBSetupStatus::NOT_ATTEMPTED: return "NOT_ATTEMPTED";
        case IBSetupStatus::SUCCESS: return "SUCCESS";
        case IBSetupStatus::FAILURE: return "FAILURE";
        case IBSetupStatus::PROTOCOL_ERROR: return "PROTOCOL_ERROR";
        case IBSetupStatus::NETWORK_ERROR: return "NETWORK_ERROR";
        default: return "UNKNOWN";
    }
}
//---------------------------------------------------------------------------------------------------------------------
