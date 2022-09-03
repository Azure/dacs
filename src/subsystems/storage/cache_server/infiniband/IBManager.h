///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IBConstants.h"
#include "IBUtils.h"
#include "cache.pb.h"
#include <pthread.h>
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <infiniband/verbs.h>
//---------------------------------------------------------------------------------------------------------------------
class QPManager;
//---------------------------------------------------------------------------------------------------------------------
class IBManager
{
    public:
        static IBManager& GetInstance();

        // Initialize IB devices if available on this node
        bool Init();

        // Check if IB is available and configured successfully
        bool IBEnabled() const;

        // Setup client<->server qp pairs and prepare to receive requests
        bool SetupQPConnections(
            /*[in ]*/ uint32_t clientLid,
            /*[in ]*/ uint8_t clientIbPort,
            /*[in ]*/ uint8_t clientMtu,
            /*[in ]*/ const ::google::protobuf::RepeatedField<::google::protobuf::uint32>& clientQpns,
            /*[out]*/ uint32_t& serverLid,
            /*[out]*/ uint8_t& serverIbPort,
            /*[out]*/ uint8_t& serverMtu,
            /*[out]*/ ::google::protobuf::RepeatedField<::google::protobuf::uint32>& serverQpns);

        struct IbDevice
        {
            uint32_t lid;
            uint8_t port;
            enum ibv_mtu mtu;
            ibutils::unique_ibv_context context;
            ibutils::unique_ibv_pd pd;
            ibutils::unique_ibv_comp_channel compEventChannel;
            ibutils::unique_ibv_cq sendCQ;
            ibutils::unique_ibv_srq receiveSRQ;
            ibutils::unique_ibv_mr mrBuffer;
            std::unique_ptr<char[]> mrBufferBlock;
        };

    private:
        IBManager();
        ~IBManager();

        // Main loop to accept incoming requests on IB
        void IBListenerThread();

        // Worker thread to handle individual requests
        void RequestHandlerThread();

        // Process a completed receive work request
        void ProcessRequest(/*[in ]*/ const ibv_wc& wc);

        // Post a memory buffer for receiving incoming requests
        void PostWrToSRQ(/*[in ]*/ uint64_t bufferAddr);

    private:

        // Whether IB is enabled and active
        bool m_ibEnabled;

        // Data structure to manage the IB device on this node
        IbDevice m_ibDevice;

        std::unique_ptr<QPManager> m_qpManager;

        // Ready Queue of IB work completions received from remote clients
        std::queue<ibv_wc>                                                  m_ibRequestQueue;
        std::mutex                                                          m_mutexIBRequestQ;
        std::condition_variable                                             m_cvQueue;
};
//---------------------------------------------------------------------------------------------------------------------