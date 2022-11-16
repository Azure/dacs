//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IConnection.h"
#include "IBUtils.h"
#include <vector>
#include <queue>
#include <string>
#include <mutex>
#include <memory>
#include <infiniband/verbs.h>
//---------------------------------------------------------------------------------------------------------------------
class ConnectionManager
{
    public:
        ConnectionManager(
            /*[in]*/ int serverPort,
            /*[in]*/ bool useLocalServer,
            /*[in]*/ const std::vector<std::string>& cacheServers);
        ~ConnectionManager();

    public:
        /* Get connection handle */
        std::unique_ptr<IConnection> GetConnection(/*[in]*/ int serverIndex);

        /* Release connection handle */
        void ReleaseConnection(
            /*[in]*/ int serverIndex,
            /*[in]*/ std::unique_ptr<IConnection> serverConnection,
            /*[in]*/ bool errorConnection);

    private:
        ConnectionManager(const ConnectionManager&);
        ConnectionManager& operator=(const ConnectionManager&);

        std::unique_ptr<IConnection> InitFreshConnection(/*[in]*/ int serverIndex);

        bool InitInfiniband();
        std::unique_ptr<IConnection> SetupIBConnections(int serverFd, int serverIndex);
    
    private:
        static constexpr int                                    MAX_CONNECTIONS_REUSE_SIZE = 128;

        const int                                               m_ServerPort;
        std::vector<std::mutex>                                 m_vecLocks;
        std::vector<std::queue<std::unique_ptr<IConnection>>>   m_vecConnectionQueue;
        std::vector<std::string>                                m_vecCacheServers;
        int                                                     m_numServerNodes;
        int                                                     m_nMaxConnsPerServer;
        const bool                                              m_bReuseServerConnections;

        // Use infiniband if IB ports are available on local device.
        bool                                                    m_bUseInfiniband;

        // Number of infiniband data connections (QPs) to be established using a single control connection.
        static constexpr int                                    IB_CONNECTIONS_PER_SERVER = 1;

        struct IbDev
        {
            uint32_t lid;
            uint8_t port;
            enum ibv_mtu mtu;
            ibutils::unique_ibv_context context;
            ibutils::unique_ibv_pd pd;
        };
        IbDev m_ibDevice;
};
//---------------------------------------------------------------------------------------------------------------------