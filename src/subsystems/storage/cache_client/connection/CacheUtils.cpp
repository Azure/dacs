///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheUtils.h"
#include "CommonLogger.h"
#include "StorageLibcApiWrapper.h"
#include "IConnection.h"
#include <sys/socket.h>
#include <iomanip>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <map>
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
        uint64_t totalWritten = connection->Send(static_cast<const char*>(buffer), nBytes);
        return totalWritten == nBytes;
    }
//---------------------------------------------------------------------------------------------------------------------
    bool WriteAllFinal(IConnection* connection, void *buffer, size_t nBytes)
    {
        uint64_t totalWritten = connection->SendAndFinalize(static_cast<const char*>(buffer), nBytes);
        return totalWritten == nBytes;
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
                totalRead += tempRead;
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
            if ((tempRead = connection->Receive(data + totalRead, size - totalRead)) > 0)
            {
                totalRead += tempRead;
            }
            else 
            {
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

        if (connection->Send(outputBuffer.data(), outputBuffer.size()) != outputBuffer.size())
        {
            LOG_ERROR("couldn't send message to remote id={}", connection->GetId());
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

        if (connection->SendAndFinalize(outputBuffer.data(), outputBuffer.size()) != outputBuffer.size())
        {
            LOG_ERROR("couldn't send message to remote id={}", connection->GetId());
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

        uint32_t protoSize = ConvertBytesToInt(metaData, REQUEST_METADATA_SIZE);

        string requestMessage(protoSize, 0);
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
    uint32_t GetServerIndex(const std::string& filePath, uint32_t numServers)
    {        
        if (numServers > 1)
        {
            static thread_local hash<string> hasher;
            auto hash_val = hasher(filePath);
            return hash_val % numServers;            
        }
        else
        {
            return 0;
        }
    }
//---------------------------------------------------------------------------------------------------------------------
    std::string getHMACSHA256(const string& key, const string& message)
    {
        string hashValue(EVP_MAX_MD_SIZE, '\0');
        unsigned int hashLen = EVP_MAX_MD_SIZE;
        
        if (!HMAC(EVP_sha256(), key.data(), key.length(),  (const unsigned char *)message.data(), message.length(),
            (unsigned char*)hashValue.data(), &hashLen))
        {
            LOG_ERROR("Failed to generate HMAC signature");
            return "";
        }

        return hashValue.substr(0, hashLen);
    }
//---------------------------------------------------------------------------------------------------------------------
    bool AddAuthParams(Request& request, const string& strCacheAccountName, const string& strCacheAccountKey)
    {
        LOG_DEBUG("AddAuthParams cache auth enabled")
        AuthParams* authParams = nullptr;
        time_t timestamp = time(NULL);
        string serializedHeaders;
        if (request.type_case() == Request::kDownloadrequest)
        {
            serializedHeaders = 
                std::string("D") + 
                request.mutable_downloadrequest()->filename() + 
                std::to_string(request.mutable_downloadrequest()->offset()) +
                std::to_string(request.mutable_downloadrequest()->length()) +
                (request.mutable_downloadrequest()->enablelock() ? "1" : "0") +
                std::to_string(timestamp);
            authParams = request.mutable_downloadrequest()->mutable_authparams();
        }
        else if (request.type_case() == Request::kUploadrequest)
        {
            serializedHeaders = 
                std::string("U") + 
                request.mutable_uploadrequest()->filename() + 
                std::to_string(request.mutable_uploadrequest()->filesize()) +
                (request.mutable_uploadrequest()->ignorelock() ? "1" : "0") +
                std::to_string(request.mutable_uploadrequest()->expiryseconds()) +
                std::to_string(timestamp);
            authParams = request.mutable_uploadrequest()->mutable_authparams();
        }
        else if (request.type_case() == Request::kGetattributerequest)
        {
            serializedHeaders = 
                std::string("G") + 
                request.mutable_getattributerequest()->filename() + 
                (request.mutable_getattributerequest()->enablelock() ? "1" : "0") +
                std::to_string(timestamp);
            authParams = request.mutable_getattributerequest()->mutable_authparams();
        }
        else if (request.type_case() == Request::kPutattributerequest)
        {
            serializedHeaders = 
                std::string("P") + 
                std::to_string(request.mutable_putattributerequest()->fileattributes_size()) + 
                std::to_string(timestamp);
            for (auto fileAttribute : request.putattributerequest().fileattributes())
            {
                serializedHeaders += fileAttribute.filename();
                serializedHeaders += std::to_string(fileAttribute.fileattribute().filesize());
            }
            authParams = request.mutable_putattributerequest()->mutable_authparams();
        }
        else if (request.type_case() == Request::kLockrequest)
        {
            serializedHeaders = 
                std::string("L") + 
                request.mutable_lockrequest()->filename() + 
                std::to_string(timestamp);
            authParams = request.mutable_lockrequest()->mutable_authparams();
        }
        else if (request.type_case() == Request::kIbinitrequest)
        {
            serializedHeaders =
                std::string("IB") +
                std::to_string(request.ibinitrequest().clientlid()) +
                std::to_string(request.ibinitrequest().clientibport()) +
                std::to_string(request.ibinitrequest().clientmtu()) +
                std::to_string(request.ibinitrequest().clientqpn_size()) +
                std::to_string(timestamp);
            authParams = request.mutable_ibinitrequest()->mutable_authparams();
        }
        else
        {
            LOG_ERROR("unkown req type: {}", request.type_case());
            return false;
        }

        authParams->set_enabled(true);
        authParams->set_accountname(strCacheAccountName);
        authParams->set_timestamp(timestamp);
        authParams->set_signature(getHMACSHA256(strCacheAccountKey, serializedHeaders));

        return authParams->signature().length() > 0;
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
        static char buf[36];

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
