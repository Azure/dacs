//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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

        FRIEND_TEST(ConnectionManagerTests, RecvTimeoutSocketOptionIsSetAfterReceive);
        FRIEND_TEST(ConnectionManagerTests, RecvTimeoutSocketOptionUpdatedForDifferentSize);
        FRIEND_TEST(ConnectionManagerTests, SndTimeoutSocketOptionIsSetAfterSend);
        friend struct ScopedMinTimeout;
};
//---------------------------------------------------------------------------------------------------------------------