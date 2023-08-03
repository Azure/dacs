//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <sys/types.h>
#include <cstdint>
#include <unistd.h>
//---------------------------------------------------------------------------------------------------------------------
class IConnection
{
    public:

        // Send buffer to remote, this might buffer the data internally and flush to remote could be pending.
        // Return number of bytes written.
        virtual int Send(
            /*[in ]*/ const char* buffer,
            /*[in ]*/ uint64_t size) = 0;

        // Send buffer to remote and flush data. This should always be called as the last send operation.
        // Return number of bytes written.
        virtual int SendAndFinalize(
            /*[in ]*/ const char* buffer,
            /*[in ]*/ uint64_t size) = 0;

        // Receive data from remote.
        // Return number of bytes read.
        virtual int Receive(
            /*[out]*/ char* buffer,
            /*[in ]*/ uint64_t size) = 0;

        // Unique ID for this connection.
        virtual int GetId() = 0;

        // Check if connection is still valid and can be reused.
        virtual bool IsValidConnection() = 0;

        // Reset all connections params so it can be reused for new requests.
        virtual void ResetConnection() = 0;

        virtual ~IConnection()  = default;
};
//---------------------------------------------------------------------------------------------------------------------