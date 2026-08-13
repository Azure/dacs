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