//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IConnection.h"
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

        void __UnitTest_SetSocketTimeoutMillis(int timeoutMillis);

        bool IsValidConnection();

        void ResetConnection();

    private:
        ssize_t TimedRead(void* buf, size_t nByte);
        ssize_t TimedWrite(const void* buf, size_t nByte);
    
        static int socketTimeoutMillis;

        int m_socketFd;
        bool m_sendAttempted;
};
//---------------------------------------------------------------------------------------------------------------------