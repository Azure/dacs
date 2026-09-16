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

#include "CacheUtils.h"
#include "CommonLogger.h"
#include "StorageLibcApiWrapper.h"
#include "IConnection.h"
#include <sys/socket.h>
#include <iomanip>
#include <openssl/sha.h>
#include <map>
#include <limits>
#include <regex>
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
namespace cacheutils
{
//---------------------------------------------------------------------------------------------------------------------
    uint64_t ConvertBytesToInt(char *data, int size)
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
    bool WriteAll(IConnection* connection, void *buffer, size_t nBytes)
    {
        const int totalWritten = connection->Send(static_cast<const char*>(buffer), nBytes);
        return totalWritten >= 0 && static_cast<size_t>(totalWritten) == nBytes;
    }
//---------------------------------------------------------------------------------------------------------------------
    bool WriteAllFinal(IConnection* connection, const void *buffer, size_t nBytes)
    {
        const int totalWritten = connection->SendAndFinalize(static_cast<const char*>(buffer), nBytes);
        return totalWritten >= 0 && static_cast<size_t>(totalWritten) == nBytes;
    }
//---------------------------------------------------------------------------------------------------------------------
    bool ReadAll(IConnection* connection, void *buffer, size_t nBytes)
    {
        uint64_t totalRead = 0;
        while (totalRead < nBytes)
        {
            int tempRead;
            if ((tempRead = connection->Receive(static_cast<char*>(buffer) + totalRead, nBytes - totalRead)) > 0)
            {
                totalRead += static_cast<uint64_t>(tempRead);
            }
            else 
            {
                LOG_WARN("Failed to download data from server, id={}, totalRead={}, tempRead={}",
                    connection->GetId(), totalRead, tempRead);
                return false;
            }
        }

        return true;
    }
//---------------------------------------------------------------------------------------------------------------------
    int ReadBytesToArray(IConnection* connection, char* data, int size)
    {
        int totalRead = 0;

        while (totalRead < size)
        {
            int tempRead;
            if ((tempRead = connection->Receive(
                data + totalRead,
                static_cast<uint64_t>(size - totalRead))) > 0)
            {
                totalRead += tempRead;
            }
            else 
            {
                LOG_WARN("Failed to read all bytes from server, id={}, totalRead={}, tempRead={}, expectedSize={}",
                    connection->GetId(), totalRead, tempRead, size);
                return totalRead;
            }
        }
        return totalRead;
    }
//---------------------------------------------------------------------------------------------------------------------
    bool SendMessageToRemote(IConnection* connection, const ::google::protobuf::Message& message)
    {
        string protoBuffer;
        if (!message.SerializeToString(&protoBuffer))
        {
            LOG_ERROR("couldn't serialize message proto");
            return false;
        }

        string outputBuffer(REQUEST_METADATA_SIZE, 0);
        ConvertIntToBytes(protoBuffer.size(), outputBuffer.data(), 0, REQUEST_METADATA_SIZE);
        
        outputBuffer += protoBuffer;

        const int bytesSent = connection->Send(outputBuffer.data(), outputBuffer.size());
        if (bytesSent < 0 || static_cast<size_t>(bytesSent) != outputBuffer.size())
        {
            LOG_ERROR("couldn't send message to remote id={}, bytesSent={}, expectedBytes={}",
                connection->GetId(), bytesSent, outputBuffer.size());
            return false;
        }
        return true;
    }
//---------------------------------------------------------------------------------------------------------------------
    bool SendMessageToRemoteFinal(IConnection* connection, const ::google::protobuf::Message& message)
    {
        string protoBuffer;
        if (!message.SerializeToString(&protoBuffer))
        {
            LOG_ERROR("couldn't serialize message proto");
            return false;
        }

        string outputBuffer(REQUEST_METADATA_SIZE, 0);
        ConvertIntToBytes(protoBuffer.size(), outputBuffer.data(), 0, REQUEST_METADATA_SIZE);
        
        outputBuffer += protoBuffer;

        const int bytesSent = connection->SendAndFinalize(outputBuffer.data(), outputBuffer.size());
        if (bytesSent < 0 || static_cast<size_t>(bytesSent) != outputBuffer.size())
        {
            LOG_ERROR("couldn't send message to remote id={}, bytesSent={}, expectedBytes={}",
                connection->GetId(), bytesSent, outputBuffer.size());
            return false;
        }
        return true;
    }
//---------------------------------------------------------------------------------------------------------------------
    bool ReadMessageFromRemote(IConnection* connection, string& message)
    {
        char metaData[REQUEST_METADATA_SIZE];
        int res = ReadBytesToArray(connection, metaData, REQUEST_METADATA_SIZE);
        if (res != REQUEST_METADATA_SIZE)
        {
            LOG_ERROR("error: Failed to read metadata from server, id={}", connection->GetId());
            return false;
        }

        const uint64_t decodedProtoSize = ConvertBytesToInt(metaData, REQUEST_METADATA_SIZE);
        if (decodedProtoSize > static_cast<uint64_t>(std::numeric_limits<int>::max()))
        {
            LOG_ERROR("Protobuf message exceeds supported size, id={}, size={}",
                connection->GetId(), decodedProtoSize);
            return false;
        }
        const int protoSize = static_cast<int>(decodedProtoSize);

        string requestMessage(static_cast<size_t>(protoSize), 0);
        res = ReadBytesToArray(connection, requestMessage.data(), protoSize);
        if (res != protoSize)
        {
            LOG_ERROR("error: Failed to read protobuf message, id={}", connection->GetId());
            return false;
        }
        message = requestMessage;
        return true;
    }
//---------------------------------------------------------------------------------------------------------------------
    uint32_t GetServerIndex(const char* filePath, uint32_t numServers)
    {
        if (numServers > 1)
        {
            static thread_local hash<string_view> hasher;
            auto hash_val = hasher(filePath);
            return hash_val % numServers;
        }
        else
        {
            return 0;
        }
    }
//---------------------------------------------------------------------------------------------------------------------
bool IsValidLastAccessTime(const char* lastAccessTime)
{

    std::string timeStr(lastAccessTime);
    int day, month, year, hour, minute, second, milliseconds;
    char timezoneIndicator;

    if (sscanf(lastAccessTime, "%d-%d-%dT%d:%d:%d.%d%c", &year, &month, &day, &hour, &minute, &second, &milliseconds, &timezoneIndicator) != 8 ||
        timezoneIndicator != 'Z') {
        return false;
    }

    LOG_DEBUG("Parsed date: {}/{}/{}-{}:{}:{}:{}", day, month, year, hour, minute, second, milliseconds);

    return (day >= 1 && day <= 31 && month >= 1 && month <= 12 && year >= 0 && hour >= 0 && hour <= 23 &&
            minute >= 0 && minute <= 59 && second >= 0 && second <= 59 && milliseconds >= 0 && milliseconds <= 999);
}
//---------------------------------------------------------------------------------------------------------------------
    std::string GetSHA256Hex(const std::string& message)
    {
        unsigned char shaHash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, message.c_str(), message.size());
        SHA256_Final(shaHash, &sha256);

        // hex encode the sha digest
        stringstream ss;
        for(auto i = 0; i < SHA256_DIGEST_LENGTH; i++)
        {
            ss << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(shaHash[i]);
        }
        return ss.str();
    }
//---------------------------------------------------------------------------------------------------------------------
    std::string GetCorrelationId()
    {
        static thread_local char buf[36];

        int offset = 0;
        for(int i = 0; i < 3; i++)
        {
            offset += sprintf(buf + offset, "%x-", rand());
        }
        offset += sprintf(buf + offset, "%x", rand());

        return std::string(buf);
    }

//---------------------------------------------------------------------------------------------------------------------
}
//---------------------------------------------------------------------------------------------------------------------
