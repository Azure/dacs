//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "IConnection.h"
#include <stdint.h>
//---------------------------------------------------------------------------------------------------------------------
class SocketConnection : public IConnection
{
    public:
        SocketConnection(int socketFd);
        ~SocketConnection();

        int Read(
            /*[out] */ char* buffer,
            /*[in ] */ int size
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
            /*[in ] */ int fileFd,
            /*[in ] */ uint64_t fileStartOffset,
            /*[in ] */ uint64_t totalLength
            );

        bool Recvfile(
            /*[in ] */ int fileFd,
            /*[in ] */ uint64_t totalLength
            );

        int GetId();
        
    private:
        const int m_socketFd;
        int m_corkState;
};
//---------------------------------------------------------------------------------------------------------------------