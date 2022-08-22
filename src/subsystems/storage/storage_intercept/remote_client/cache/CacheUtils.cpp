///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheUtils.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptConstants.h"
#include "StorageInterceptLogger.h"
#include "StorageDlsymWrapper.h"
#include "poll.h"
#include <sys/socket.h>
#include <iomanip>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <mutex>
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
    ssize_t TimedWrite(int fd, const void *buf, size_t nByte)
    {
        struct pollfd pollFDs[1];
        memset(pollFDs, 0 , sizeof(pollFDs));
        pollFDs[0].fd = fd;
        pollFDs[0].events = POLLOUT;

        int pollResult = poll(pollFDs, 1, SOCKET_TIMEOUT_MILLIS);
        if (pollResult == 0)
        {
            LOG_WARN("Timeout writing to server, fd={}", fd);
            return -1;
        }
        if (pollResult < 0 || pollFDs[0].revents != POLLOUT)
        {
            LOG_WARN("Error listening to write event, fd={}", fd);
            return -1;
        }

        return send(fd, buf, nByte, 0);
    }
//---------------------------------------------------------------------------------------------------------------------
    bool WriteAll(int fd, void *buffer, size_t nBytes)
    {
        uint64_t totalWritten = 0;
        while (totalWritten < nBytes)
        {
            int bufferLen = nBytes - totalWritten;
            int tempWrite;
            if ((tempWrite = TimedWrite(
                fd, 
                static_cast<const char*>(buffer) + totalWritten,
                bufferLen)) > 0)
            {
                totalWritten += tempWrite;
            }
            else 
            {
                LOG_WARN("couldn't complete write to server: fd:{}. wrote: {}, error: {}",
                    fd, totalWritten, tempWrite);
                return false;
            }
        }

        return true;
    }
//---------------------------------------------------------------------------------------------------------------------
    ssize_t TimedRead(int fd, void *buf, size_t nByte)
    {
        struct pollfd pollFDs[1];
        memset(pollFDs, 0 , sizeof(pollFDs));
        pollFDs[0].fd = fd;
        pollFDs[0].events = POLLIN;

        int pollResult = poll(pollFDs, 1, SOCKET_TIMEOUT_MILLIS);
        if (pollResult == 0)
        {
            LOG_WARN("Timeout reading from server, fd={}", fd);
            return -1;
        }
        if (pollResult < 0 || pollFDs[0].revents != POLLIN)
        {
            LOG_WARN("Error listening to read event, fd={}", fd);
            return -1;
        }

        return recv(fd, buf, nByte, 0);
    }
//---------------------------------------------------------------------------------------------------------------------
    bool ReadAll(int fd, void *buffer, size_t nBytes)
    {
        uint64_t totalRead = 0;
        while (totalRead < nBytes)
        {
            int tempRead;
            if ((tempRead = TimedRead(fd, static_cast<char*>(buffer) + totalRead, nBytes - totalRead)) > 0)
            {
                totalRead += tempRead;
            }
            else 
            {
                LOG_WARN("Failed to download data from server, fd={}, totalRead={}, tempRead={}",
                    fd, totalRead, tempRead);
                return false;
            }
        }

        return true;
    }
//---------------------------------------------------------------------------------------------------------------------
    int ReadBytesToArray(int clientFd, char* data, int size)
    {
        int totalRead = 0;

        while (totalRead < size)
        {
            int tempRead;
            if ((tempRead = TimedRead(clientFd, data + totalRead, size - totalRead)) > 0)
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
    bool SendMessageToRemote(int clientFd, const ::google::protobuf::Message& message)
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

        if (TimedWrite(clientFd, outputBuffer.data(), outputBuffer.size()) != outputBuffer.size())
        {
            LOG_ERROR("couldn't send message to remote fd={}", clientFd);
            return false;
        }
        return true;
    }
//---------------------------------------------------------------------------------------------------------------------
    bool ReadMessageFromRemote(int serverFd, string& message)
    {
        char metaData[REQUEST_METADATA_SIZE];
        int res = ReadBytesToArray(serverFd, metaData, REQUEST_METADATA_SIZE);
        if (res != REQUEST_METADATA_SIZE)
        {
            LOG_ERROR("error: Failed to read metadata from server, fd={}", serverFd);
            return false;
        }

        uint32_t protoSize = ConvertBytesToInt(metaData, REQUEST_METADATA_SIZE);

        string requestMessage(protoSize, 0);
        res = ReadBytesToArray(serverFd, requestMessage.data(), protoSize);
        if (res != protoSize)
        {
            LOG_ERROR("error: Failed to read protobuf message, fd={}", serverFd);
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
    bool AddAuthParams(Request& request)
    {
        if (!StorageInterceptConfig::GetInstance().GetCacheAuthEnabled())
        {
            return true;
        }

        static const std::string& accountName = StorageInterceptConfig::GetInstance().GetCacheAuthAccountName();
        static const std::string& accountKey = StorageInterceptConfig::GetInstance().GetCacheAuthAccountKey();

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
        else
        {
            LOG_ERROR("unkown req type: {}", request.type_case());
            return false;
        }

        authParams->set_enabled(true);
        authParams->set_accountname(accountName);
        authParams->set_timestamp(timestamp);
        authParams->set_signature(getHMACSHA256(accountKey, serializedHeaders));

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
    std::string GetCacheKey(const std::string& remotePath, off64_t chunkOffset)
    {
        // To uniquely identify a chunk within a cluster we need 
        // <account name>/<container name>/<filepath within the container>:<file offset>
        static string strCachePrefix;
        static std::once_flag onceFlag;
        std::call_once(onceFlag, [](){
            if (StorageInterceptConfig::GetInstance().GetRemoteStoreType() ==
                StorageInterceptConstants::ConfigValRemoteTypeBlob)
            {
                const bool isDynamicContainer = StorageInterceptConfig::GetInstance().IsDynamicContainerMount();
                strCachePrefix = StorageInterceptConfig::GetInstance().GetStorageAccountName() + "/";

                if (!isDynamicContainer)
                {
                    strCachePrefix += StorageInterceptConfig::GetInstance().GetBlobContainerName() + "/";
                }
            }
            else if (StorageInterceptConfig::GetInstance().GetRemoteStoreType() ==
                StorageInterceptConstants::ConfigValRemoteTypeAdls)
            {
                strCachePrefix = StorageInterceptConfig::GetInstance().GetAdlsHostName() + "/" +
                    StorageInterceptConfig::GetInstance().GetAdlsClientId() + "/";
            }

            if (StorageInterceptConfig::GetInstance().GetCacheEncryptionEnabled())
            {
                // If encryption is enabled then the file contents will be different depending on the encryption
                // key used by the client, hence treating the same file if encryted using different keys as different.
                strCachePrefix += StorageInterceptConfig::GetInstance().GetCacheEncryptionKey() + "/";
            }
        });

        return GetSHA256Hex(strCachePrefix + remotePath + ":" + std::to_string(chunkOffset));
    }
//---------------------------------------------------------------------------------------------------------------------
}
//---------------------------------------------------------------------------------------------------------------------
