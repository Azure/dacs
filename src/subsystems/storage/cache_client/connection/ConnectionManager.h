// Copyright (c) Microsoft Corporation.
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

#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IConnection.h"
#include "IBUtils.h"
#include "TestFriend.h"
#include "hashing/HashingFactory.h"
#include "cache.pb.h"
#include <vector>
#include <map>
#include <queue>
#include <string>
#include <mutex>
#include <condition_variable>
#include <shared_mutex>
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <infiniband/verbs.h>
//---------------------------------------------------------------------------------------------------------------------
class CacheClientTests;
class ConsistentHashingTests;
//---------------------------------------------------------------------------------------------------------------------
class ConnectionManager
{
    public:
        ConnectionManager(
            /*[in]*/ const uint16_t serverPort,
            /*[in]*/ const bool useLocalServer,
            /*[in]*/ const std::vector<std::string>& cacheServers,
            /*[in]*/ const uint32_t nMaxConnectionsReuseSize,
            /*[in]*/ const bool bReuseServerConnections,
            /*[in]*/ const bool bUseInfiniband,
            /*[in]*/ const bool bEnableCacheServerDiscovery,
            /*[in]*/ const uint32_t nCacheServerDiscoveryIntervalInSeconds,
            /*[in]*/ const std::string& cacheDiscoveryEndpoint,
            /*[in]*/ const bool bEnableConsistentHashing,
            /*[in]*/ const int nSocketSndbufSize,
            /*[in]*/ const int nSocketRcvbufSize
        );
        ~ConnectionManager();

        /* Get connection handle */
        std::unique_ptr<IConnection> GetConnection(/*[in]*/ const std::string& cacheserver);

        /* Release connection handle */
        void ReleaseConnection(
            /*[in]*/ const std::string& cacheserver,
            /*[in]*/ std::unique_ptr<IConnection> serverConnection,
            /*[in]*/ const bool errorConnection);

        std::string GetCacheServerHostname(const std::string& strCacheKey) const;
        std::vector<std::string> GetAllCacheServerHostnames() const;

    private:
        enum HandshakeStatus
        {
            SUCCESS,
            FAILURE
        };

        enum class IBSetupStatus
        {
            NOT_ATTEMPTED,
            SUCCESS,
            FAILURE,
            PROTOCOL_ERROR,
            NETWORK_ERROR
        };

        static const char* ToStringIBSetupStatus(IBSetupStatus status);

        ConnectionManager(const ConnectionManager&);
        ConnectionManager& operator=(const ConnectionManager&);

        std::unique_ptr<IConnection> CreateSocketAndConnect(const std::string& hostname, uint16_t port) const;
        std::unique_ptr<IConnection> InitFreshConnection(/*[in]*/ const std::string& cacheserver);

        // Waits up to timeoutMs for sockfd to become writable (i.e. non-blocking connect
        // completed). Uses poll() so it is safe for fd numbers >= FD_SETSIZE.
        // Returns true if the socket is writable within the timeout, false otherwise.
        static bool WaitForSocketWritable(int sockfd, int timeoutMs);

        static bool IsIbZeroCopyEnabled();

        bool InitInfiniband();
        std::unique_ptr<IConnection> SetupIBConnections(std::unique_ptr<IConnection> socketConnection, const std::string& cacheserver);
        bool PerformIBHandshake(IConnection* connection, const std::string& cacheserver);
        HandshakeStatus NewIBHandshake(IConnection* connection, const std::string& cacheserver) const;

        void UpdateCacheServers(const std::vector<std::string> &vecServers);
        std::unique_ptr<std::vector<std::string>> FetchLatestCacheServers() const;
        void MonitorCacheServers();
    
        const int                                                                   m_ServerPort;
        const int                                                                   m_nMaxConnectionsReuseSize;
        mutable std::mutex                                                          m_lockCacheServerConnections;
        mutable std::shared_mutex                                                   m_sharedLockCacheServerData;
        std::unordered_map<std::string, std::queue<std::unique_ptr<IConnection>>>   m_mapConnectionQueue;
        std::vector<std::string>                                                    m_vecCacheServers;
        int                                                                         m_nMaxConnsPerServer;
        const bool                                                                  m_bReuseServerConnections;
        const bool                                                                  m_bEnableCacheServerDiscovery;
        const uint32_t                                                              m_nCacheServerDiscoveryIntervalInSeconds;
        const std::string                                                           m_cacheDiscoveryEndpoint;
        const int                                                                   m_nSocketSndbufSize;
        const int                                                                   m_nSocketRcvbufSize;

        // Use infiniband if IB ports are available on local device.
        bool                                                                        m_bUseInfiniband;
        // Whether the zero-copy IB upload path is enabled. 
        const bool                                                                  m_bIbZeroCopyEnabled;
        std::mutex                                                                  m_lockIBHandshakeStatusMap;
        // Number of infiniband data connections (QPs) to be established using a single control connection.
        const static constexpr int                                                        IB_CONNECTIONS_PER_SERVER = 1;

        struct IbDev
        {
            uint32_t lid;
            uint8_t port;
            enum ibv_mtu mtu;
            ibutils::unique_ibv_context context;
            ibutils::unique_ibv_pd pd;
            int max_sge;  // max SGEs supported by device for send WRs
        };
        IbDev m_ibDevice;

        std::unordered_map<std::string, HandshakeStatus>                            m_mapIbHandshakeStatus;
        std::unordered_map<std::string, IBSetupStatus>                              m_mapIbSetupStatus;
        std::mutex                                                                  m_lockIbSetupStatusMap;
        // Long running thread for refreshing cacheservers
        std::thread                                                                 m_cacheServerRefresherThread;
        std::atomic<bool>                                                           m_bStopCacheServerRefresher = false;
        std::mutex                                                                  m_mtxStopCacheServerRefresher;
        std::condition_variable                                                     m_cvStopCacheServerRefresher;
        std::unique_ptr<BaseHasher> m_hasher;
        const HashingStrategy m_hashingStrategy;

    // Friends for testing
    DACS_FRIEND_TEST(CacheClientTests, RefreshCacheServersWithDiscoveryEndpoint);
    DACS_FRIEND_TEST(ConsistentHashingTests, ScaleUpDown);
    DACS_FRIEND_TEST(ConsistentHashingTests, DistributionAnalysis);
    DACS_FRIEND_TEST(ConnectionManagerTests, WaitForSocketWritableWorksAboveFdSetSize);
    DACS_FRIEND_TEST(ConnectionManagerTests, IsIbZeroCopyEnabledRespectsEnvVar);
};
//---------------------------------------------------------------------------------------------------------------------