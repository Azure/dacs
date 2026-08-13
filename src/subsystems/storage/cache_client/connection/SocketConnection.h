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
#include "gtest/gtest_prod.h"
//---------------------------------------------------------------------------------------------------------------------
class SocketConnection : public IConnection
{
    public:
        SocketConnection(int socketFd);
        ~SocketConnection();

        int Send(
            /*[in ]*/ const char* buffer,
            /*[in ]*/ uint64_t size);

        int SendAndFinalize(
            /*[in ]*/ const char* buffer,
            /*[in ]*/ uint64_t size);

        int Receive(
            /*[out]*/ char* buffer,
            /*[in ]*/ uint64_t size);

        int GetId();

        bool IsValidConnection();

        void ResetConnection();

    private:
        ssize_t TimedRead(void* buf, size_t nByte);
        ssize_t TimedWrite(const void* buf, size_t nByte);

        static uint32_t socketTimeoutMillisMin;
        static uint32_t socketTimeoutMillisMax;
        static uint32_t socketTimeoutMillisPerMB;
        static int GetTimeoutMillis(size_t ioSizeInBytes);

        int m_socketFd;
        bool m_sendAttempted;
        int m_rcvTimeoutMillis;
        int m_sndTimeoutMillis;
        uint64_t m_requestBytesWritten;

        FRIEND_TEST(ConnectionManagerTests, RecvTimeoutSocketOptionIsSetAfterReceive);
        FRIEND_TEST(ConnectionManagerTests, RecvTimeoutSocketOptionUpdatedForDifferentSize);
        FRIEND_TEST(ConnectionManagerTests, RequestBytesWrittenControlsReceiveTimeout);
        FRIEND_TEST(ConnectionManagerTests, SndTimeoutSocketOptionIsSetAfterSend);
        friend struct ScopedMinTimeout;
};
//---------------------------------------------------------------------------------------------------------------------