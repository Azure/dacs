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
#include <memory>
#include <infiniband/verbs.h>
//---------------------------------------------------------------------------------------------------------------------
class IBConnection : public IConnection
{
    public:
        IBConnection(ibutils::unique_ibv_qp qp, ibutils::unique_ibv_comp_channel eventChannel,
            ibutils::unique_ibv_cq cq, ibutils::unique_ibv_mr mr, std::unique_ptr<char[]> mrBuffer,
            bool directMrEnabled = true);
        ~IBConnection();

        int Send(
            /*[in ]*/ const char* buffer,
            /*[in ]*/ uint64_t size);

        int SendAndFinalize(
            /*[in ]*/ const char* buffer,
            /*[in ]*/ uint64_t size);

        int Receive(
            /*[out]*/ char* buffer,
            /*[in ]*/ uint64_t size);

        // Unique ID for this connection.
        int GetId();

        // Check if connection is still valid and can be reused.
        bool IsValidConnection();

        // Reset all connections params so it can be reused for new requests.
        void ResetConnection();

    private:

        // Send all the accumulated data in the local buffer in one shot to the remote.
        bool FinalizeWrite();

        // Send using 2-SGE: header from MR buffer + data from caller's externally-registered buffer.
        bool FinalizeWriteWithExternalBuffer(const char* dataBuffer, uint64_t dataSize);

        // Deregister any externally registered memory region.
        bool DeregisterExternalMr();

        // Poll the CQ until the SEND WC is received. Handles an early-arriving RECV WC
        // by recording m_responseLength so Receive() does not block again.
        // Sets m_qpError and returns false on any failure.
        bool WaitForSendWC(uint64_t bytesSent);

        // Helper to calculate the timeout for IB operations based on the amount of data to be transferred.
        uint64_t CalculateTimeoutMillis(uint64_t ioByteCount) const;

    private:
        // Maximum read/write timeout
        static constexpr uint64_t IB_TIMEOUT_MILLIS_PER_MB = 500; // 0.5 seconds per MB
        static constexpr uint64_t IB_MIN_TIMEOUT_MILLIS = 5000; // 5 seconds
        static constexpr uint64_t IB_MAX_TIMEOUT_MILLIS = 100000; // 100 seconds

        // Threshold above which we register the caller's buffer directly instead of memcpy
        static constexpr uint64_t DIRECT_REG_THRESHOLD = 4 * 1024 * 1024;

        // The queue pair from where we read and write data from/to the remote IB device
        ibutils::unique_ibv_qp m_qp;

        // The event channel to wait for send and receive completions
        ibutils::unique_ibv_comp_channel m_eventChannel;

        // The completion queue where the send/receive work requests are posted
        ibutils::unique_ibv_cq m_cq;

        // Registered memory region and buffer to send/receive data to/from IB device
        ibutils::unique_ibv_mr m_mr;
        std::unique_ptr<char[]> m_mrBuffer;

        // Number of bytes written so far to the local buffer
        uint32_t m_writeOffset;

        // The length of the response in bytes received from remote
        int32_t m_responseLength;

        // The data read so far by the application client from the locally stored response buffer
        uint32_t m_readOffset;

        // Indicates error in IB connection to prevent this connection from being reused
        bool m_qpError;

        // Flag to ensure we attempt send to remote only once
        bool m_sendAttempted;

        // Externally registered memory region for direct-MR sends (caller's buffer)
        struct ibv_mr* m_externalMr{nullptr};

        // Whether the device supports 2-SGE sends needed for the direct-MR path
        const bool m_directMrEnabled;
};
//---------------------------------------------------------------------------------------------------------------------