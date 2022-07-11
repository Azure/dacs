//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include <vector>
#include <queue>
#include <string>
#include <mutex>
//---------------------------------------------------------------------------------------------------------------------
class ConnectionManager
{
    public:
        ConnectionManager(int serverPort, bool useLocalServer, const std::vector<std::string>& cacheServers);
        ~ConnectionManager();

    public:
        /* Get connection handle */
        int GetFd(/*[in]*/ int serverIndex);

        /* Release connection handle */
        void ReleaseFd(/*[in]*/ int serverIndex, /*[in]*/ int serverFd, /*[in]*/ bool errorFd);

    private:
        int InitFreshConnection(/*[in]*/ int serverIndex);
        bool IsValidConnection(/*[in]*/ int connectionFd);
    
    private:
        static constexpr int                    CONNECT_TIMEOUT = 2;
        static constexpr int                    MAX_CONNECTIONS_REUSE_SIZE = 128;

        const int                               m_ServerPort;
        std::vector<std::mutex>                 m_vecLocks;
        std::vector<std::queue<int> >           m_vecFdQueue;
        std::vector<std::string>                m_vecCacheServers;
        int                                     m_numServerNodes;
        int                                     m_nMaxConnsPerServer;
        const bool                              m_bReuseServerConnections;
};
//---------------------------------------------------------------------------------------------------------------------