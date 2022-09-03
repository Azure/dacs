//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <sys/types.h>
#include <cstdint>
//---------------------------------------------------------------------------------------------------------------------
class IConnection
{
    public:

        // Read data from remote endpoint into the buffer
        virtual int Read(
            /*[out] */ char* buffer,
            /*[in ] */ int size
            ) = 0;

        // Write data from the buffer to remote. Data might get buffered and network send might happen in WriteFinal
        virtual int Write(
            /*[in ] */ char* buffer,
            /*[in ] */ int size
            ) = 0;

        // Write data to remote. This should be the last write on the connection and data will be flushed
        virtual int WriteFinal(
            /*[in ] */ char* buffer,
            /*[in ] */ int size
            ) = 0;

        // Used to directly send data from local file (fileFd) to remote
        virtual bool Sendfile(
            /*[in ] */ int fileFd,
            /*[in ] */ uint64_t fileStartOffset,
            /*[in ] */ uint64_t totalLength
            ) = 0;

        // Identifier to uniquely identify this connection
        virtual int GetId() = 0;

        virtual ~IConnection() = default;
};
//---------------------------------------------------------------------------------------------------------------------