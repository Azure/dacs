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

        // Used to receive data from remote directly into a local file (fileFd).
        // This is the inverse of Sendfile and uses splice() for zero-copy on TCP sockets.
        // Returns true if all bytes were received and written successfully.
        virtual bool Recvfile(
            /*[in ] */ int fileFd,
            /*[in ] */ uint64_t totalLength
            ) = 0;

        // Identifier to uniquely identify this connection
        virtual int GetId() = 0;

        virtual ~IConnection() = default;
};
//---------------------------------------------------------------------------------------------------------------------