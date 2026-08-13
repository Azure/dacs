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