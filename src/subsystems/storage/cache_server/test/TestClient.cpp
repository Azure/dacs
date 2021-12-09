///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "Constants.h"
#include "Utils.h"
#include "SocketUtils.h"
#include "UploadRequest.h"
#include "LockRequest.h"
#include "DownloadRequest.h"
#include "cache.pb.h"
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <chrono>
#include <netdb.h>
#include <cstring>
#include <signal.h>
#include <thread>
#include <vector>
#include <fstream>
#include <mutex>
#include <queue>
#include <xxhash.h>
#include <atomic>
#include "cache.pb.cc"
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
string CacherServer  = "127.0.0.1";
int ServerPort = 20000;
queue<int> ServerConnections;
mutex LockPool;
std::atomic_uint_fast64_t totalConnections(0);
int TotalTests = 0;
int TestsFailed = 0;
#define RUNTEST(x) { \
        cout << "Running test: " << #x << endl; \
        TotalTests++; \
        if (!(x)) \
        { \
            cout << "###TEST FAILED###: " << #x << endl; \
            TestsFailed++; \
        } \
    }
//---------------------------------------------------------------------------------------------------------------------
int GetConnection()
{
    {
        lock_guard lock(LockPool);
        if (!ServerConnections.empty())
        {
            int serverFd = ServerConnections.front();
            ServerConnections.pop();
            return serverFd;
        }
    }

    cout << "New connection to server: " << CacherServer.c_str() << ". port: " << ServerPort << endl;
    totalConnections++;

    sockaddr_in sockAddr;
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_port = htons(ServerPort);
    if( (inet_pton(AF_INET, CacherServer.c_str(), &(sockAddr.sin_addr))) < 0)
    {
        cout << "inet_pton error" << endl;
        return -1;
    }
    
    int clientSocketFd;
    if ( (clientSocketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        cout << "unable to create a socket connection" << endl;
        return -1;
    }
    
    if (connect(clientSocketFd, (sockaddr *) &( sockAddr ), sizeof(sockAddr) ) < 0 )
    {
        cout << "connection failed error" << endl;
        return -1;
    }
    return clientSocketFd;
}
//---------------------------------------------------------------------------------------------------------------------
void ReleaseConnection(int fd)
{
    lock_guard lock(LockPool);
    ServerConnections.push(fd);
}
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
int ReadBytesToArray(int clientFd, char* data, int size)
{
    int totalRead = 0;

    while (totalRead < size)
    {
        int tempRead;
        if ((tempRead = read(clientFd, data + totalRead, size - totalRead)) > 0)
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
        cout << "couldn't serialize message proto" << endl;
        return false;
    }

    string outputBuffer(REQUEST_METADATA_SIZE, 0);
    ConvertIntToBytes(protoBuffer.size(), outputBuffer.data(), 0, REQUEST_METADATA_SIZE);
    
    outputBuffer += protoBuffer;

    if (write(clientFd, outputBuffer.data(), outputBuffer.size()) != outputBuffer.size())
    {
        cout << "couldn't write message to remote fd: " << clientFd << endl;
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
        std::cout << "error: Failed to read metadata from server" << endl;
        return false;
    }

    uint32_t protoSize = ConvertBytesToInt(metaData, REQUEST_METADATA_SIZE);
    //std::cout << "Cache server protobuf size: " << protoSize << endl;

    string requestMessage(protoSize, 0);
    res = ReadBytesToArray(serverFd, requestMessage.data(), protoSize);
    if (res != protoSize)
    {
        std::cout << "error: Failed to read protobuf message" << endl;
        return false;
    }
    message = requestMessage;
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool TestLockAPI(const string& filename, bool locked)
{
    int serverFd = GetConnection();
    if (serverFd < 0)
    { 
        cout << "failed to get server connection" << endl;
	    return false;
    }
    Request request;

    LockFileRequest* lockRequest = request.mutable_lockrequest();
    lockRequest->set_filename(filename.c_str());

    if (!SendMessageToRemote(serverFd, request))
    {
        cout << "couldn't send message to server" << endl;
        close(serverFd);
        return false;
    }

    string requestMessage;
    if (!ReadMessageFromRemote(serverFd, requestMessage))
    {
        cout << "couldn't read message from server" << endl;
        close(serverFd);
        return false;
    }

    LockFileResponse response;
    if (!response.ParseFromString(requestMessage))
    {
        std::cout << "Failed to parse protobuf message" << endl;
        close(serverFd);
        return false;
    }

    bool status = true;
    if (locked)
    {
        if (response.result() != LockFileResponse::SUCCESS)
        {
            //std::cout << "error: file already locked" << endl;
            status = false;
        }
    }
    else
    {
        if (response.result() == LockFileResponse::SUCCESS)
        {
            //std::cout << "error: file not locked" << endl;
            status = false;
        }
    }

    ReleaseConnection(serverFd);
    return status;
}
//---------------------------------------------------------------------------------------------------------------------
bool TestParallelLock(int parallelism, int durationInSeconds)
{
	auto t1 = std::chrono::high_resolution_clock::now();

    std::atomic_uint_fast64_t totalLocked(0);
    std::atomic_bool stop(false);
    std::vector<std::thread *> threads;

    string filePrefix = "paralelleltest-" + std::to_string(rand()) + "-";
    for (int threadId = 0; threadId < parallelism; threadId++)
    {
        auto t = new std::thread([&filePrefix, &totalLocked, parallelism, &stop]()
        {
            int i = 0;
            while (!stop)
            {
                string fileName = filePrefix + std::to_string(i);
                bool bRet = TestLockAPI(fileName, true);
                if (bRet)
                {
                    totalLocked++;
                }
                i = (i + 1) % parallelism;
            }
        });
        threads.push_back(t);
    }

    std::this_thread::sleep_for(std::chrono::seconds(durationInSeconds));
    stop = true;

    for (int i = 0; i < threads.size(); ++i)
    {
        auto t = threads[i];
        t->join();
        delete t;
    }

	auto t2 = std::chrono::high_resolution_clock::now();

	auto durationSum = chrono::duration_cast<chrono::milliseconds>(t2 - t1);

    if (totalLocked == parallelism)
    {
        return true;
    }

    cout << "Total locked: " << totalLocked << ", expected: " << parallelism << endl;
    //cout << "Total time in millis: " << durationSum.count() << endl;

    return false;
}
//---------------------------------------------------------------------------------------------------------------------
bool TestUploadAPI(
    const string& filename,
    uint64_t filesize,
    bool ignoreLock,
    UploadResponse_Result expectedResult,
    uint64_t *hashValue = nullptr)
{
    int serverFd = GetConnection();
    if (serverFd < 0)
    { 
        cout << "failed to get server connection" << endl;
        return false;
    }
    Request request;

    //cout << "testupload. filesize: " << filesize << endl;

    UploadRequest* uploadRequest = request.mutable_uploadrequest();
    uploadRequest->set_filename(filename.c_str());
    uploadRequest->set_filesize(filesize);    
    uploadRequest->set_ignorelock(ignoreLock);

    if (!SendMessageToRemote(serverFd, request))
    {
        cout << "couldn't send message to server" << endl;
        close(serverFd);
        return false;
    }

    //cout << "upload request sent to server" << endl;

    if (filesize > 0)
    {
        uint64_t totalWritten = 0;
        XXH64_state_t* hashState = XXH64_createState();
        XXH64_reset(hashState, 0);
        while (totalWritten < filesize)
        {
            int BUF_SIZE = 1024*1024;
            char data[BUF_SIZE];
            int bufferLen = (int)std::min((uint64_t)BUF_SIZE, filesize - totalWritten);

            // add rand chars to first few bytes
            for (int i = 0; i < std::min(100, bufferLen); i++)
            {
                data[i] = (char)(rand() % 256);
            }

            int tempWrite;
            if ((tempWrite = write(serverFd, data, bufferLen)) > 0)
            {
                totalWritten += tempWrite;
            }
            else 
            {
                cout << "couldn't write() message to server. wrote: " << totalWritten <<", error: " <<
                    tempWrite << endl;
                close(serverFd);
                return false;
            }

            if (XXH64_update(hashState, data, bufferLen) != 0)
            {
                cout << "xxh64_update failed" << endl;
                close(serverFd);
                return false;
            }
        }
        if (hashValue != nullptr)
        {
            *hashValue = XXH64_digest(hashState);
        }
    }

    //cout << "sent file to server" << endl;

    string requestMessage;
    if (!ReadMessageFromRemote(serverFd, requestMessage))
    {
        cout << "couldn't read message from server" << endl;
        close(serverFd);
        return false;
    }

    UploadResponse response;
    if (!response.ParseFromString(requestMessage))
    {
        std::cout << "error: Failed to parse protobuf message" << endl;
        close(serverFd);
        return false;
    }

    bool status = true;
    if (response.result() != expectedResult)
    {
        std::cout << "error: upload failed: response.result() != expectedResult:" << response.result() << ", " <<
            expectedResult << endl;
        status = false;
    }

    ReleaseConnection(serverFd);
    return status;
}
//---------------------------------------------------------------------------------------------------------------------
bool TestDownloadAPI(
    const string& filename,
    uint64_t offset,
    uint64_t filesize,
    bool lock,
    DownloadResponse_Result expectedResult,
    uint64_t *hashValue = nullptr)
{
    int serverFd = GetConnection();
    if (serverFd < 0)
    { 
        cout << "failed to get server connection" << endl;
	    return false;
    }
    Request request;

    DownloadRequest* downloadRequest = request.mutable_downloadrequest();
    downloadRequest->set_filename(filename.c_str());
    downloadRequest->set_offset(offset);
    downloadRequest->set_length(filesize);
    downloadRequest->set_enablelock(lock);

    if (!SendMessageToRemote(serverFd, request))
    {
        cout << "couldn't send message to server" << endl;
        close(serverFd);
        return false;
    }

    string requestMessage;
    if (!ReadMessageFromRemote(serverFd, requestMessage))
    {
        cout << "couldn't read message from server" << endl;
        close(serverFd);
        return false;
    }

    DownloadResponse response;
    if (!response.ParseFromString(requestMessage))
    {
        std::cout << "error: Failed to parse protobuf message" << endl;
        close(serverFd);
        return false;
    }

    if (response.result() != expectedResult)
    {
        std::cout << "error: download failed: response.result() != expectedResult:" << response.result() << ", " <<
            expectedResult << endl;
        close(serverFd);
        return false;
    }

    if (response.result() == DownloadResponse::SUCCESS)
    {
        if (filesize != response.filesize())
        {
            std::cout << "error: download failed: invalid filesize" << endl;
            close(serverFd);
            return false;
        }
        uint64_t totalRead = 0;
        int tempRead;
        XXH64_state_t* hashState = XXH64_createState();
        XXH64_reset(hashState, 0);

        while (totalRead < filesize)
        {
            int BUFSIZE = 1024*1024;
            char data[BUFSIZE];
            if ((tempRead = read(serverFd, data, (int)std::min((uint64_t)BUFSIZE, filesize - totalRead))) > 0)
            {
                totalRead += tempRead;
            }
            else 
            {
                cout << "couldn't read() message from server" << endl;
                close(serverFd);
                return false;
            }
            if (XXH64_update(hashState, data, tempRead) != 0)
            {
                cout << "xxh64_update failed" << endl;
                close(serverFd);
                return false;
            }
        }
        if (totalRead != filesize)
        {
            cout << "error: totalRead != filesize" << endl;
            close(serverFd);
            return false;
        }
        if (hashValue != nullptr && *hashValue != XXH64_digest(hashState))
        {
            cout << "hashValue doesn't match" << endl;
            close(serverFd);
            return false;
        }
    }

    ReleaseConnection(serverFd);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool TestParallelUploadDownload(int parallelism, int durationInSeconds)
{
    std::atomic_uint_fast64_t totalFailed(0);
    std::atomic_bool stop(false);
    std::vector<std::thread *> threads;

    string filePrefix = "paralelleltest-" + std::to_string(rand()) + "-";
    for (int threadId = 0; threadId < parallelism; threadId++)
    {
        auto t = new std::thread([&filePrefix, &totalFailed, threadId, parallelism, &stop]()
        {
            int i = 0;
            while (!stop)
            {
                string fileName = filePrefix + std::to_string(threadId) + "-" + to_string(i);

                bool passed = true;

                uint64_t hashValue = 0;
                passed &= TestDownloadAPI(fileName, 0, 100, true, DownloadResponse::NOT_FOUND_GOT_LOCK);
                passed &= TestDownloadAPI(fileName, 0, 100, true, DownloadResponse::NOT_FOUND_ALREADY_LOCKED);
                passed &= TestUploadAPI(fileName, 100, false, UploadResponse::SUCCESS, &hashValue);
                passed &= TestDownloadAPI(fileName, 0, 100, true, DownloadResponse::SUCCESS, &hashValue);
                passed &= TestDownloadAPI(fileName, 0, 100, true, DownloadResponse::SUCCESS, &hashValue);
                if (!passed)
                {
                    totalFailed++;
                    break;
                }
                i++;
            }
        });
        threads.push_back(t);
    }

    std::this_thread::sleep_for(std::chrono::seconds(durationInSeconds));
    stop = true;

    for (int i = 0; i < threads.size(); ++i)
    {
        auto t = threads[i];
        t->join();
        delete t;
    }

    return totalFailed == 0;
}
//---------------------------------------------------------------------------------------------------------------------
bool TestPutAttributeAPI(PutAttributeRequest& putRequest, PutAttributeResponse_Result expectedResult)
{
    int serverFd = GetConnection();
    if (serverFd < 0)
    { 
        cout << "failed to get server connection" << endl;
        return false;
    }

    Request request;
    request.set_allocated_putattributerequest(&putRequest);
    if (!SendMessageToRemote(serverFd, request))
    {
        cout << "couldn't send message to server" << endl;
        close(serverFd);
        return false;
    }

    string requestMessage;
    if (!ReadMessageFromRemote(serverFd, requestMessage))
    {
        cout << "couldn't read message from server" << endl;
        close(serverFd);
        return false;
    }

    PutAttributeResponse response;
    if (!response.ParseFromString(requestMessage))
    {
        std::cout << "error: Failed to parse protobuf message" << endl;
        close(serverFd);
        return false;
    }

    bool status = true;
    if (response.result() != expectedResult)
    {
        std::cout << "error: putattribute failed: response.result() != expectedResult:" << response.result() <<
            ", " << expectedResult << endl;
        status = false;
    }

    request.release_putattributerequest();
    ReleaseConnection(serverFd);
    return status;
}
//---------------------------------------------------------------------------------------------------------------------
bool TestGetAttributeAPI(
    const string& filename,
    bool enableLock,
    GetAttributeResponse_Result expectedResult,
    bool isDir,
    uint64_t filesize,
    uint64_t accessedtime,
    uint64_t modifiedtime)
{
    int serverFd = GetConnection();
    if (serverFd < 0)
    { 
        cout << "failed to get server connection" << endl;
        return false;
    }

    Request request;
    GetAttributeRequest* getAttributeRequest = request.mutable_getattributerequest();
    getAttributeRequest->set_filename(filename);
    getAttributeRequest->set_enablelock(enableLock);
    if (!SendMessageToRemote(serverFd, request))
    {
        cout << "couldn't send message to server" << endl;
        close(serverFd);
        return false;
    }

    string requestMessage;
    if (!ReadMessageFromRemote(serverFd, requestMessage))
    {
        cout << "couldn't read message from server" << endl;
        close(serverFd);
        return false;
    }

    GetAttributeResponse response;
    if (!response.ParseFromString(requestMessage))
    {
        std::cout << "error: Failed to parse protobuf message" << endl;
        close(serverFd);
        return false;
    }

    bool status = true;
    if (response.result() != expectedResult)
    {
        std::cout << "error: getattribute failed: response.result() != expectedResult:" << response.result() <<
            ", " << expectedResult << endl;
        status = false;
    }

    if (expectedResult == GetAttributeResponse::SUCCESS)
    {
        if (response.fileattribute().isdir() != isDir ||
            response.fileattribute().filesize() != filesize ||
            response.fileattribute().accessedtime() != accessedtime ||
            response.fileattribute().modifiedtime() != modifiedtime)
            {
                std::cout << "error: getattribute failed: return values don't match" << endl;
                status = false;
            }
    }

    ReleaseConnection(serverFd);
    return status;
}
//---------------------------------------------------------------------------------------------------------------------
bool TestParallelAttributeCache(int parallelism, int durationInSeconds)
{
    std::atomic_uint_fast64_t totalFailed(0);
    std::atomic_bool stop(false);
    std::vector<std::thread *> threads;

    string filePrefix = "paralellelattrtest-" + std::to_string(rand()) + "-";
    for (int threadId = 0; threadId < parallelism; threadId++)
    {
        auto t = new std::thread([&filePrefix, &totalFailed, threadId, parallelism, &stop]()
        {
            int i = 0;
            while (!stop)
            {
                string fileName = filePrefix + std::to_string(threadId) + "-" + to_string(i);
                string fileName2 = fileName + "-2";
                string fileName3 = fileName + "-3";

                bool passed = true;

                PutAttributeRequest putRequest;
                {
                    auto fileAttr = putRequest.add_fileattributes();
                    fileAttr->set_filename(fileName);
                    auto attr = fileAttr->mutable_fileattribute();
                    attr->set_isdir(true);
                    attr->set_filesize(100);
                    attr->set_accessedtime(101);
                    attr->set_modifiedtime(102);
                }
                passed &= TestGetAttributeAPI(fileName, false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0);
                passed &= TestGetAttributeAPI(
                    fileName, true, GetAttributeResponse::NOT_FOUND_GOT_LOCK, false, 0, 0, 0);
                passed &= TestGetAttributeAPI(fileName, true, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0);
                passed &= TestGetAttributeAPI(fileName, false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0);
                passed &= TestPutAttributeAPI(putRequest, PutAttributeResponse::SUCCESS);
                passed &= TestGetAttributeAPI(fileName, false, GetAttributeResponse::SUCCESS, true, 100, 101, 102);

                {
                    putRequest.clear_fileattributes();
                    auto fileAttr = putRequest.add_fileattributes();
                    fileAttr->set_filename(fileName2);
                    auto attr = fileAttr->mutable_fileattribute();
                    attr->set_isdir(true);
                    attr->set_filesize(0);
                    attr->set_accessedtime(201);
                    attr->set_modifiedtime(202);

                    fileAttr = putRequest.add_fileattributes();
                    fileAttr->set_filename(fileName3);
                    attr = fileAttr->mutable_fileattribute();
                    attr->set_isdir(false);
                    attr->set_filesize(300);
                    attr->set_accessedtime(301);
                    attr->set_modifiedtime(302);
                }
                passed &= TestGetAttributeAPI(fileName2, false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0);
                passed &= TestGetAttributeAPI(
                    fileName3, true, GetAttributeResponse::NOT_FOUND_GOT_LOCK, false, 0, 0, 0);
                passed &= TestGetAttributeAPI(fileName3, true, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0);
                passed &= TestGetAttributeAPI(fileName2, false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0);
                passed &= TestPutAttributeAPI(putRequest, PutAttributeResponse::SUCCESS);
                passed &= TestGetAttributeAPI(fileName2, true, GetAttributeResponse::SUCCESS, true, 0, 201, 202);
                passed &= TestGetAttributeAPI(fileName3, false, GetAttributeResponse::SUCCESS, false, 300, 301, 302);

                if (!passed)
                {
                    totalFailed++;
                    break;
                }
                i++;
            }
        });
        threads.push_back(t);
    }

    std::this_thread::sleep_for(std::chrono::seconds(durationInSeconds));
    stop = true;

    for (int i = 0; i < threads.size(); ++i)
    {
        auto t = threads[i];
        t->join();
        delete t;
    }

    return totalFailed == 0;
}
//---------------------------------------------------------------------------------------------------------------------
int TestLimits()
{
    // Assuming cache server is started with cache limits set to
    // AttributeLimit = 1024
    // CacheSizeLimit = 100 * 1024 * 1024 bytes;
    int AttributeLimit = 1024;
    int CacheSizeLimit = 100 * 1024 * 1024;

    cout << "Test attribute cache limits" << endl;
    for (int i = 0; i < AttributeLimit; i++)
    {
        string fileName = string("test-") + to_string(i);
        PutAttributeRequest putRequest;
        {
            auto fileAttr = putRequest.add_fileattributes();
            fileAttr->set_filename(fileName);
            auto attr = fileAttr->mutable_fileattribute();
            attr->set_isdir(false);
            attr->set_filesize(100);
            attr->set_accessedtime(101);
            attr->set_modifiedtime(102);
        }
        RUNTEST(TestGetAttributeAPI(fileName, false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
        RUNTEST(TestPutAttributeAPI(putRequest, PutAttributeResponse::SUCCESS));
        RUNTEST(TestGetAttributeAPI(fileName, false, GetAttributeResponse::SUCCESS, false, 100, 101, 102));
    }

    string extraFileName = "extraFile";
    PutAttributeRequest putRequest;
    {
        auto fileAttr = putRequest.add_fileattributes();
        fileAttr->set_filename(extraFileName);
        auto attr = fileAttr->mutable_fileattribute();
        attr->set_isdir(false);
        attr->set_filesize(100);
        attr->set_accessedtime(101);
        attr->set_modifiedtime(102);
    }
    RUNTEST(TestGetAttributeAPI(extraFileName, false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestPutAttributeAPI(putRequest, PutAttributeResponse::INTERNAL_ERROR));
    RUNTEST(TestGetAttributeAPI(extraFileName, false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));

    cout << "Test cache size limits" << endl;

    uint64_t hashValue = 0;
    for (int i = 0; i < 100; i++)
    {
        string fileName = string("test-") + to_string(i);
        RUNTEST(TestDownloadAPI(fileName, 0, 1024*1024, false, DownloadResponse::NOT_FOUND));
        RUNTEST(TestUploadAPI(fileName, 1024*1024, true, UploadResponse::SUCCESS, &hashValue));
        RUNTEST(TestDownloadAPI(fileName, 0, 1024*1024, false, DownloadResponse::SUCCESS, &hashValue));
    }
    RUNTEST(TestDownloadAPI(extraFileName, 0, 1024*1024, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestUploadAPI(extraFileName, 1024*1024, true, UploadResponse::SUCCESS, &hashValue) == false);
    RUNTEST(TestDownloadAPI(extraFileName, 0, 1024*1024, false, DownloadResponse::NOT_FOUND, &hashValue));

    cout << "Test complete. Total tests: " << TotalTests << ". Failed: " << TestsFailed << endl;
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    if (argc > 1)
    {
        return TestLimits();
    }

    cout << "Test lock APIs" << endl;
    RUNTEST(TestLockAPI("testlock1", true));
    RUNTEST(TestLockAPI("testlock1", false));
    RUNTEST(TestLockAPI("testlock1", false));
    RUNTEST(TestLockAPI("testlock1", false));
    RUNTEST(TestLockAPI("testlock1", false));
    RUNTEST(TestLockAPI("testlock2", true));
    RUNTEST(TestLockAPI("testlock2", false));
    RUNTEST(TestLockAPI("testlock1", false));
    RUNTEST(TestLockAPI("testlock1", false));
    RUNTEST(TestLockAPI("testlock2", false));

    cout << "Basic upload/download tests" << endl;
    uint64_t hashValue = 0;
    RUNTEST(TestDownloadAPI("test1", 0, 100, true, DownloadResponse::NOT_FOUND_GOT_LOCK));
    RUNTEST(TestDownloadAPI("test1", 0, 100, true, DownloadResponse::NOT_FOUND_ALREADY_LOCKED));

    RUNTEST(TestUploadAPI("test1", 100, false, UploadResponse::SUCCESS, &hashValue));

    RUNTEST(TestDownloadAPI("test1", 0, 100, true, DownloadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI("test1", 0, 100, true, DownloadResponse::SUCCESS, &hashValue));
    
    RUNTEST(TestDownloadAPI("test2", 0, 100, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestDownloadAPI("test2", 0, 100, true, DownloadResponse::NOT_FOUND_GOT_LOCK));
    RUNTEST(TestDownloadAPI("test2", 0, 100, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestDownloadAPI("test2", 0, 100, true, DownloadResponse::NOT_FOUND_ALREADY_LOCKED));
    RUNTEST(TestUploadAPI("test2", 1024*1024*10 + 1, false, UploadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI("test2", 0, 1024*1024*10 + 1, false, DownloadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI("test2", 0, 1024*1024*10 + 1, true, DownloadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI("test2", 0, 1024*1024*10 + 1, false, DownloadResponse::SUCCESS));

    cout << "Partial download tests" << endl;
    RUNTEST(TestDownloadAPI("test2", 1, 1024*1024*10, false, DownloadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI("test2", 1, 1024*1024*20, false, DownloadResponse::SUCCESS) == false);
    RUNTEST(TestDownloadAPI("test2", 1024*1024*10, 1, false, DownloadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI("test2", 1024*1024*10+1, 0, false, DownloadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI("test2", 1024*1024*10+1, 10, false, DownloadResponse::SUCCESS) == false);
    RUNTEST(TestDownloadAPI("test2", 1024*1024*10+2, 0, false, DownloadResponse::INVALID_OFFSET_LENGTH));
    RUNTEST(TestDownloadAPI("test2", 1024*1024*20, 10, false, DownloadResponse::INVALID_OFFSET_LENGTH));

    RUNTEST(TestDownloadAPI("test1", 0, 100, false, DownloadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI("test1", 0, 100, true, DownloadResponse::SUCCESS));

    RUNTEST(TestUploadAPI("test3", 1, true, UploadResponse::SUCCESS));
    RUNTEST(TestUploadAPI("test3", 10, true, UploadResponse::SUCCESS) == false);
    RUNTEST(TestUploadAPI("test4", 10, false, UploadResponse::SUCCESS) == false);
    RUNTEST(TestUploadAPI("test5", 0, true, UploadResponse::SUCCESS));
    RUNTEST(TestUploadAPI("test5", 10, true, UploadResponse::SUCCESS) == false);

    RUNTEST(TestDownloadAPI("test4", 0, 1024*1024, true, DownloadResponse::NOT_FOUND_GOT_LOCK));
    RUNTEST(TestUploadAPI("test4", 1024*1024, false, UploadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI("test4", 0, 1024*1024, true, DownloadResponse::SUCCESS, &hashValue));

    cout << "test special chars" << endl;
    RUNTEST(TestDownloadAPI(
        "test5/dir/%$^&*(^%$#^*////\\\\////", 0, 1024*1024+1, true, DownloadResponse::NOT_FOUND_GOT_LOCK));
    RUNTEST(TestUploadAPI(
        "test5/dir/%$^&*(^%$#^*////\\\\////", 1024*1024+1, false, UploadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI(
        "test5/dir/%$^&*(^%$#^*////\\\\////", 0, 1024*1024+1, true, DownloadResponse::SUCCESS, &hashValue));

    cout << "test zero length files" << endl;
    RUNTEST(TestDownloadAPI("test6", 0, 0, true, DownloadResponse::NOT_FOUND_GOT_LOCK));
    RUNTEST(TestUploadAPI("test6", 0, false, UploadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI("test6", 0, 0, true, DownloadResponse::SUCCESS));

    cout << "test long file names" << endl;
    string longFile1(127, '1');
    string longFile2(128, '2');
    RUNTEST(TestUploadAPI(longFile1, 1024*1024+1, true, UploadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI(longFile1, 0, 1024*1024+1, true, DownloadResponse::SUCCESS, &hashValue));
    RUNTEST(TestUploadAPI(longFile2, 1024*1024+1, true, UploadResponse::SUCCESS) == false);
    RUNTEST(TestDownloadAPI(longFile2, 0, 1024*1024+1, true, DownloadResponse::FILENAME_LIMIT_EXCEEDED));

    cout << "test large file uploads" << endl;
    RUNTEST(TestDownloadAPI("test7", 0, 1024*1024*1024, true, DownloadResponse::NOT_FOUND_GOT_LOCK));
    RUNTEST(TestUploadAPI("test7", 1024*1024*1024, false, UploadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI("test7", 0, 1024*1024*1024, true, DownloadResponse::SUCCESS, &hashValue));
    RUNTEST(TestUploadAPI("test8", (uint64_t)4*1024*1024*1024, true, UploadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI("test8", 0, (uint64_t)4*1024*1024*1024, true, DownloadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI("test8", 0, (uint64_t)4*1024*1024*1024, true, DownloadResponse::SUCCESS, &hashValue));
    RUNTEST(TestUploadAPI(
        "test9", (uint64_t)4*1024*1024*1024 + 1, true, UploadResponse::SUCCESS, &hashValue) == false);
    RUNTEST(TestDownloadAPI(
        "test9", 0, (uint64_t)4*1024*1024*1024 + 1, false, DownloadResponse::NOT_FOUND, &hashValue));

    cout << "test parallel lock" << endl;
    RUNTEST(TestParallelLock(200, 5));
    RUNTEST(TestParallelLock(1, 2));
    RUNTEST(TestParallelLock(100, 5));

    cout << "test parallel upload/download" << endl;
    RUNTEST(TestParallelUploadDownload(200, 5));
    RUNTEST(TestParallelUploadDownload(1, 2));
    RUNTEST(TestParallelUploadDownload(500, 10));

    cout << "Test attribute cache" << endl;
    PutAttributeRequest putRequest;
    {
        auto fileAttr = putRequest.add_fileattributes();
        fileAttr->set_filename("test1");
        auto attr = fileAttr->mutable_fileattribute();
        attr->set_isdir(true);
        attr->set_filesize(100);
        attr->set_accessedtime(101);
        attr->set_modifiedtime(102);
    }
    RUNTEST(TestGetAttributeAPI("test1", false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestGetAttributeAPI("test1", true, GetAttributeResponse::NOT_FOUND_GOT_LOCK, false, 0, 0, 0));
    RUNTEST(TestGetAttributeAPI("test1", true, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestGetAttributeAPI("test1", false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestPutAttributeAPI(putRequest, PutAttributeResponse::SUCCESS));
    RUNTEST(TestGetAttributeAPI("test1", false, GetAttributeResponse::SUCCESS, true, 100, 101, 102));

    {
        putRequest.clear_fileattributes();
        auto fileAttr = putRequest.add_fileattributes();
        fileAttr->set_filename("test2");
        auto attr = fileAttr->mutable_fileattribute();
        attr->set_isdir(true);
        attr->set_filesize(0);
        attr->set_accessedtime(201);
        attr->set_modifiedtime(202);

        fileAttr = putRequest.add_fileattributes();
        fileAttr->set_filename("test3");
        attr = fileAttr->mutable_fileattribute();
        attr->set_isdir(false);
        attr->set_filesize(300);
        attr->set_accessedtime(301);
        attr->set_modifiedtime(302);
    }
    RUNTEST(TestGetAttributeAPI("test2", false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestGetAttributeAPI("test3", true, GetAttributeResponse::NOT_FOUND_GOT_LOCK, false, 0, 0, 0));
    RUNTEST(TestGetAttributeAPI("test3", true, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestGetAttributeAPI("test2", false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestPutAttributeAPI(putRequest, PutAttributeResponse::SUCCESS));
    RUNTEST(TestGetAttributeAPI("test2", true, GetAttributeResponse::SUCCESS, true, 0, 201, 202));
    RUNTEST(TestGetAttributeAPI("test3", false, GetAttributeResponse::SUCCESS, false, 300, 301, 302));

    cout << "Test parallel attribute cache" << endl;
    RUNTEST(TestParallelAttributeCache(200, 5));
    RUNTEST(TestParallelAttributeCache(1, 2));
    RUNTEST(TestParallelAttributeCache(500, 10));

    cout << "Total established connections: " << totalConnections << endl;
    cout << "Test complete. Total tests: " << TotalTests << ". Failed: " << TestsFailed << endl;
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------