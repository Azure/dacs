/// Copyright (c) Microsoft Corporation.
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

#include "ConnectionUtils.h"
#include "Constants.h"
#include "Logger.h"
#include <chrono>
#include <iostream>
#include <unistd.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
uint64_t ConvertBytesToInt(char* data, int size)
{
    uint64_t result = 0;
    uint64_t mask = 255;
    for (int i = 0; i < size; ++i)
    {
        uint64_t d = (uint64_t)data[i] & mask;
        uint64_t x = d << (8 * (size - (i + 1)));
        result |= (x);
    }
    return result;
}
//---------------------------------------------------------------------------------------------------------------------
void ConvertIntToBytes(uint64_t source, char* destination, int index, int size)
{
    uint64_t allOnes = 255;
    for (int i = 0; i < size; ++i)
    {
        int numBitsToShift = 8 * (size - (i + 1));
        uint64_t mask = source >> numBitsToShift;
        destination[index + i] = (char)(allOnes & mask);
    }
}
//---------------------------------------------------------------------------------------------------------------------
int ReadBytesToArray(IConnection& clientConnection, char* data, int size)
{
    int totalRead = 0;
    int tempRead;

    while (totalRead < size)
    {
        if ((tempRead = clientConnection.Read(data + totalRead, size - totalRead)) > 0)
        {
            totalRead += tempRead;
        }
        else 
        {
            LOG_WARN("read client incomplete, id: {}, errno: {}:{}, expected: {}, read: {} requestid: unavailable",
                clientConnection.GetId(), errno, strerror(errno), size, totalRead);
            return totalRead;
        }
    }
    return totalRead;
}
//---------------------------------------------------------------------------------------------------------------------
bool SendMessageToRemote(IConnection& clientConnection, const ::google::protobuf::Message& message, const string& requestid)
{
    string protoBuffer;
    if (!message.SerializeToString(&protoBuffer))
    {
        LOG_WARN_REQID_STR("couldn't serialize message proto", requestid);
        return false;
    }

    string outputBuffer(REQUEST_METADATA_SIZE, 0);
    ConvertIntToBytes(protoBuffer.size(), outputBuffer.data(), 0, REQUEST_METADATA_SIZE);
    outputBuffer += protoBuffer;
    if (clientConnection.Write(outputBuffer.data(), outputBuffer.size()) != outputBuffer.size())
    {
        LOG_WARN_REQID_STR("couldn't write response to client id: {}, errno: {}:{}",
            requestid,
            clientConnection.GetId(), errno, strerror(errno));
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool SendMessageToRemoteFinal(IConnection& clientConnection, const ::google::protobuf::Message& message, const string& requestid)
{
    string protoBuffer;
    if (!message.SerializeToString(&protoBuffer))
    {
        LOG_WARN_REQID_STR("couldn't serialize message proto", requestid);
        return false;
    }

    string outputBuffer(REQUEST_METADATA_SIZE, 0);
    ConvertIntToBytes(protoBuffer.size(), outputBuffer.data(), 0, REQUEST_METADATA_SIZE);
    outputBuffer += protoBuffer;
    if (clientConnection.WriteFinal(outputBuffer.data(), outputBuffer.size()) != outputBuffer.size())
    {
        LOG_WARN_REQID_STR("couldn't write response to client id: {}, errno: {}:{}",
            requestid,
            clientConnection.GetId(), errno, strerror(errno));
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------