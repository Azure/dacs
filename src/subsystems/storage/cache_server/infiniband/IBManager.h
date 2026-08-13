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
#include <atomic>
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
        // Safe to call duplicately from same buffer.
        void PostWrToSRQ(/*[in ]*/ uint64_t bufferAddr);

        // Marks a buffer as consumed/removed from the SRQ so it can be re-posted via PostWrToSRQ()
        void MarkBufferRemovedFromSRQ(/*[in]*/ uint64_t bufferAddr);

        // Returns the index into m_bufferPosted for a given buffer address
        int BufferIndex(/*[in]*/ uint64_t addr) const;

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

        // Per-buffer posted flag: false = available (not in SRQ), true = posted in SRQ.
        // Guards PostWrToSRQ() against duplicate calls from concurrent code paths.
        std::atomic<bool> m_bufferPosted[MAX_RECEIVE_BUFFERS]{};
};
//---------------------------------------------------------------------------------------------------------------------