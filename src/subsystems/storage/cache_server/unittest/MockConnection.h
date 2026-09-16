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

// In-memory IConnection test double that serves a fixed byte buffer to Read()
// and discards anything written. Shared across cache server unit tests.
//---------------------------------------------------------------------------------------------------------------------
#pragma once

#include "IConnection.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

//---------------------------------------------------------------------------------------------------------------------
class MockConnection : public IConnection
{
public:
    MockConnection() = default;
    explicit MockConnection(std::vector<char> readData) : m_readData(std::move(readData)) {}

    int Read(char* buffer, int size) override
    {
        int available = static_cast<int>(m_readData.size()) - m_readPos;
        if (size <= 0 || available <= 0)
        {
            return 0; // simulate EOF / closed peer
        }

        int toRead = std::min(size, available);
        std::memcpy(buffer, m_readData.data() + m_readPos, static_cast<size_t>(toRead));
        m_readPos += toRead;
        return toRead;
    }

    int Write(char*, int size) override { return size; }
    int WriteFinal(char*, int size) override { return size; }
    bool Sendfile(int, uint64_t, uint64_t) override { return true; }
    bool Recvfile(int, uint64_t) override { return true; }
    int GetId() override { return 4242; }

private:
    std::vector<char> m_readData;
    int m_readPos = 0;
};
//---------------------------------------------------------------------------------------------------------------------
