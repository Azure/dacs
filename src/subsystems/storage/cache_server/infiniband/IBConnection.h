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
#include <stdint.h>
#include <infiniband/verbs.h>
//---------------------------------------------------------------------------------------------------------------------
class IBConnection : public IConnection
{
    public:
        IBConnection(ibutils::shared_ibv_qp qp, char* buffer, uint32_t mrKey, int bytesReceived);
        ~IBConnection();

        int Read(
            /*[out]*/ char* buffer,
            /*[in ]*/ int size
            );

        int Write(
            /*[in ] */ char* buffer,
            /*[in ] */ int size
            );

        int WriteFinal(
            /*[in ] */ char* buffer,
            /*[in ] */ int size
            );

        bool Sendfile(
            /*[in ]*/ int fileFd,
            /*[in ]*/ uint64_t fileStartOffset,
            /*[in ]*/ uint64_t totalLength
            );

        bool Recvfile(
            /*[in ]*/ int fileFd,
            /*[in ]*/ uint64_t totalLength
            );

        int GetId();

    private:
        bool FinalizeWrite();

    private:

        // The queue pair from where we read and write data from/to the remote IB device
        ibutils::shared_ibv_qp m_qp;

        // The buffer where data is read/written. We re-use the same buffer for read and write as both never happens
        // simultaneously
        char* m_buffer;

        // The key required to access the memory region pointed to by the buffer
        const uint32_t m_mrKey;

        // The amount of data read from remote client into the buffer, this should be <= IB_BUFFER_SIZE
        // This is const as we read data from remote in one shot.
        const int m_nDataReadFromRemote;

        // Offset for the next read request
        int m_readOffset;

        // Offset upto which data has been written into the buffer
        int m_writeOffset;

        // Flag to ensure we attempt to send only once after all data is written into the buffer
        bool m_sendAttempted;
};
//---------------------------------------------------------------------------------------------------------------------