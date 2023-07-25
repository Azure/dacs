///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "Constants.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "UploadRequest.h"
#include "LockRequest.h"
#include "DownloadRequest.h"
#include "IBConstants.h"
#include "IBUtils.h"
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
#include <list>
#include <xxhash.h>
#include <atomic>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <infiniband/verbs.h>
#include "cache.pb.cc"
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace ibutils;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
string CacherServer  = "127.0.0.1";
int ServerPort = 20000;
queue<int> ServerConnections;
mutex LockPool;
std::atomic_uint_fast64_t totalConnections(0);
int TotalTests = 0;
int TestsFailed = 0;
list<string> FailedTests;

#define RUNTEST(x) { \
        cout << "Running test: " << #x << endl; \
        TotalTests++; \
        if (!(x)) \
        { \
            cout << "###TEST FAILED###: " << __LINE__ << " - " << #x << endl; \
            FailedTests.emplace_back(std::to_string(__LINE__) + ":" + #x); \
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
string GetUniqueFilename(const string& filename)
{
    static thread_local boost::uuids::random_generator uuidGenerator;
    boost::uuids::uuid uuid = uuidGenerator();
    return boost::uuids::to_string(uuid).substr(0, 12) + filename;
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

    string filePrefix = GetUniqueFilename("paralelleltest-" + std::to_string(rand()) + "-");
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
    uint64_t *hashValue = nullptr,
    const string& groupid = "",
    uint32_t ttlInSeconds = 0)
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
    uploadRequest->set_filename(filename);
    uploadRequest->set_filesize(filesize);    
    uploadRequest->set_ignorelock(ignoreLock);
    if (!groupid.empty())
    {
        uploadRequest->set_groupid(groupid);
    }
    if (ttlInSeconds > 0)
    {
        uploadRequest->set_expiryseconds(ttlInSeconds);
    }

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

    if (response.result() != expectedResult)
    {
        std::cout << "error: upload failed: response.result() != expectedResult:" << response.result() << ", " <<
            expectedResult << endl;
        close(serverFd);
        return false;
    }

    ReleaseConnection(serverFd);
    return true;
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
bool TestDeleteAPI(
    const string& groupid,
    DeleteResponse_Result expectedResult)
{
    int serverFd = GetConnection();
    if (serverFd < 0)
    { 
        cout << "failed to get server connection" << endl;
	    return false;
    }
    Request request;

    DeleteRequest* deleteRequest = request.mutable_deleterequest();
    deleteRequest->set_groupid(groupid.c_str());

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

    DeleteResponse response;
    if (!response.ParseFromString(requestMessage))
    {
        std::cout << "error: Failed to parse protobuf message" << endl;
        close(serverFd);
        return false;
    }

    if (response.result() != expectedResult)
    {
        std::cout << "error: delete failed: response.result() != expectedResult:" << response.result() << ", " <<
            expectedResult << endl;
        close(serverFd);
        return false;
    }

    ReleaseConnection(serverFd);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool TestParallelUploadDownload(int parallelism,
                                int durationInSeconds,
                                bool downloadEnabled = true,
                                std::string groupId = "")
{
    std::atomic_uint_fast64_t totalFailed(0);
    std::atomic_bool stop(false);
    std::vector<std::thread *> threads;

    string filePrefix = GetUniqueFilename("paralelleltest-" + std::to_string(rand()) + "-");
    for (int threadId = 0; threadId < parallelism; threadId++)
    {
        auto t = new std::thread([&filePrefix, &totalFailed, threadId, parallelism, &stop, downloadEnabled, groupId]()
        {
            int i = 0;
            while (!stop)
            {
                string fileName = filePrefix + std::to_string(threadId) + "-" + to_string(i);

                bool passed = true;

                uint64_t hashValue = 0;

                if(downloadEnabled)
                {
                    passed &= TestDownloadAPI(fileName, 0, 100, true, DownloadResponse::NOT_FOUND_GOT_LOCK);
                    passed &= TestDownloadAPI(fileName, 0, 100, true, DownloadResponse::NOT_FOUND_ALREADY_LOCKED);
                }

                bool ignoreLock = downloadEnabled ? false : true; // If download is disabled, ignore lock needed for upload to suceed.
                passed &= TestUploadAPI(fileName, 100, ignoreLock, UploadResponse::SUCCESS, &hashValue, groupId);

                if(downloadEnabled)
                {
                    passed &= TestDownloadAPI(fileName, 0, 100, true, DownloadResponse::SUCCESS, &hashValue);
                    passed &= TestDownloadAPI(fileName, 0, 100, true, DownloadResponse::SUCCESS, &hashValue);
                }

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
void __UploadDownloadBytesCounter(bool /*[in]*/ downloadMode,
                                  std::atomic_bool& /*[in]*/ stop,
                                  std::atomic_uint_fast64_t& /*[out]*/ totalBytesTransferred,
                                  std::atomic_uint_fast64_t& /*[out]*/ fileNamePrefixCounter)
{
    string filePrefix = "tput_tester_";
    uint64_t fileSize = 100 * 1024 * 1024; // 100 MB
    while(!stop)
    {
        if(downloadMode)
        {
            int suffix = (rand() % fileNamePrefixCounter);
            if(TestDownloadAPI(filePrefix + to_string(suffix), 0, fileSize, true, DownloadResponse::SUCCESS))
            {
                totalBytesTransferred += fileSize;
            }
        }
        else
        {
            if(TestUploadAPI(
                   filePrefix + to_string(fileNamePrefixCounter.fetch_add(1)), fileSize, true, UploadResponse::SUCCESS))
            {
                totalBytesTransferred += fileSize;
            }
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
void MeasurePerf(int parallelism, uint64_t GBUpload, uint64_t GBDownload)
{
    // To track throughput
    const uint64_t toUploadBytes = GBUpload * 1024 * 1024 * 1024;
    const uint64_t toDownloadBytes = GBDownload * 1024 * 1024 * 1024;

    cout << "Measuring upload throughput with data size " << toUploadBytes << " bytes" << endl;
    cout << "Measuring download throughput with data size " << toDownloadBytes << " bytes" << endl;

    std::atomic_uint_fast64_t totalBytesUploaded(0);
    std::atomic_uint_fast64_t totalBytesDownloaded(0);
    std::atomic_uint_fast64_t fileNamePrefixCounter(0);
    int64_t uploadDurationMs, downloadDurationMs;

    // Test upload throughput
    {
        cout << "Starting upload throughput test" << endl;
        auto start_time = std::chrono::high_resolution_clock::now();
        std::vector<std::thread*> threads;
        std::atomic_bool stop(false);
        for(int threadId = 0; threadId < parallelism; threadId++)
        {
            auto t = new std::thread(__UploadDownloadBytesCounter,
                                     /*downloadMode*/ false,
                                     std::ref(stop),
                                     std::ref(totalBytesUploaded),
                                     std::ref(fileNamePrefixCounter));
            threads.push_back(t);
        }

        while(totalBytesUploaded < toUploadBytes)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        stop = true;

        for(int i = 0; i < threads.size(); ++i)
        {
            auto t = threads[i];
            t->join();
            delete t;
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        uploadDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    }

    // Test download throughput
    {
        cout << "Starting download throughput test" << endl;
        auto start_time = std::chrono::high_resolution_clock::now();
        std::vector<std::thread*> threads;
        std::atomic_bool stop(false);
        for(int threadId = 0; threadId < parallelism; threadId++)
        {
            auto t = new std::thread(__UploadDownloadBytesCounter,
                                     /*downloadMode*/ true,
                                     std::ref(stop),
                                     std::ref(totalBytesDownloaded),
                                     std::ref(fileNamePrefixCounter));
            threads.push_back(t);
        }

        while(totalBytesDownloaded < toDownloadBytes)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        stop = true;

        for(int i = 0; i < threads.size(); ++i)
        {
            auto t = threads[i];
            t->join();
            delete t;
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        downloadDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    }

    cout << "Upload duration in ms: " << uploadDurationMs << " bytes: " << totalBytesUploaded << endl;
    cout << "Download duration in ms: " << downloadDurationMs << " bytes: " << totalBytesDownloaded << endl;
    cout << "Parallelism: " << parallelism << endl;

    cout << "--upload_throughput_mbps " << totalBytesUploaded / (uploadDurationMs * 1000) << " "
         << "--download_throughput_mbps " << totalBytesDownloaded / (downloadDurationMs * 1000) << " "
         << "--upload_size_bytes " << totalBytesUploaded << " "
         << "--download_size_bytes " << totalBytesDownloaded << " "
         << "--upload_time_ms " << uploadDurationMs << " "
         << "--download_time_ms " << downloadDurationMs << " " << endl;
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

    string filePrefix = GetUniqueFilename("paralellelattrtest-" + std::to_string(rand()) + "-");
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
    int CacheSizeLimit = 104857600;

    cout << "Test attribute cache limits" << endl;
    for (int i = 0; i < AttributeLimit; i++)
    {
        string fileName = GetUniqueFilename(string("test-") + to_string(i));
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

    string extraFileName = GetUniqueFilename("extraFile");
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
        string fileName = GetUniqueFilename(string("test-") + to_string(i));
        RUNTEST(TestDownloadAPI(fileName, 0, 1024*1024, false, DownloadResponse::NOT_FOUND));
        RUNTEST(TestUploadAPI(fileName, 1024*1024, true, UploadResponse::SUCCESS, &hashValue, "groupid1"));
        RUNTEST(TestDownloadAPI(fileName, 0, 1024*1024, false, DownloadResponse::SUCCESS, &hashValue));
    }
    RUNTEST(TestDownloadAPI(extraFileName, 0, 1, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestUploadAPI(extraFileName, 1, true, UploadResponse::SUCCESS, &hashValue) == false);
    RUNTEST(TestDownloadAPI(extraFileName, 0, 1024*1024, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestUploadAPI(extraFileName, 1024*1024, true, UploadResponse::SUCCESS, &hashValue) == false);
    RUNTEST(TestDownloadAPI(extraFileName, 0, 1024*1024, false, DownloadResponse::NOT_FOUND, &hashValue));

    RUNTEST(TestDeleteAPI("groupid1", DeleteResponse::SUCCESS));

    std::this_thread::sleep_for(std::chrono::seconds(3));

    for (int i = 0; i < 99; i++)
    {
        string fileName = GetUniqueFilename(string("test-") + to_string(i));
        RUNTEST(TestDownloadAPI(fileName, 0, 1024*1024, false, DownloadResponse::NOT_FOUND));
        RUNTEST(TestUploadAPI(fileName, 1024*1024, true, UploadResponse::SUCCESS, &hashValue, "groupid1"));
        RUNTEST(TestDownloadAPI(fileName, 0, 1024*1024, false, DownloadResponse::SUCCESS, &hashValue));
    }

    string fileName1 = GetUniqueFilename("test-99");
    RUNTEST(TestDownloadAPI(fileName1, 0, 1024*1024, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestUploadAPI(fileName1, 1024*1024, true, UploadResponse::SUCCESS, &hashValue, "groupid2"));
    RUNTEST(TestDownloadAPI(fileName1, 0, 1024*1024, false, DownloadResponse::SUCCESS, &hashValue));

    string fileName2 = GetUniqueFilename("test-100");
    RUNTEST(TestDownloadAPI(fileName2, 0, 1024*1024, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestUploadAPI(fileName2, 1024*1024, true, UploadResponse::SUCCESS, &hashValue, "groupid2") == false);
    RUNTEST(TestDownloadAPI(fileName2, 0, 1024*1024, false, DownloadResponse::NOT_FOUND, &hashValue));

    RUNTEST(TestDeleteAPI("groupid2", DeleteResponse::SUCCESS));
    std::this_thread::sleep_for(std::chrono::seconds(3));

    RUNTEST(TestDownloadAPI(fileName1, 0, 1024*1024, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestDownloadAPI(fileName2, 0, 1024*1024, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestUploadAPI(fileName2, 1024*1024, true, UploadResponse::SUCCESS, &hashValue, "", 1));
    RUNTEST(TestDownloadAPI(fileName2, 0, 1024*1024, false, DownloadResponse::SUCCESS, &hashValue));

    RUNTEST(TestDownloadAPI(fileName1, 0, 1024*1024, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestUploadAPI(fileName1, 1024*1024, true, UploadResponse::SUCCESS, &hashValue, "groupid2") == false);
    RUNTEST(TestDownloadAPI(fileName1, 0, 1024*1024, false, DownloadResponse::NOT_FOUND, &hashValue));

    std::this_thread::sleep_for(std::chrono::seconds(3));

    RUNTEST(TestDownloadAPI(fileName1, 0, 1024*1024, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestUploadAPI(fileName1, 1024*1024, true, UploadResponse::SUCCESS, &hashValue, "groupid2"));
    RUNTEST(TestDownloadAPI(fileName1, 0, 1024*1024, false, DownloadResponse::SUCCESS, &hashValue));

    RUNTEST(TestDownloadAPI(fileName2, 0, 1024*1024, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestUploadAPI(fileName2, 1024*1024, true, UploadResponse::SUCCESS, &hashValue, "", 1) == false);
    RUNTEST(TestDownloadAPI(fileName2, 0, 1024*1024, false, DownloadResponse::NOT_FOUND, &hashValue));

    cout << "Test complete. Total tests: " << TotalTests << ". Failed: " << TestsFailed << endl;
    if (TestsFailed > 0)
    {
        cout << "Ensure cache server is started as: ./CacheServer 20000 <folder path on ssd> 104857600 1024 1" << endl;
    }
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
bool TestMetadata(const google::protobuf::Map<string, string>& metadata)
{
    int serverFd = GetConnection();
    if (serverFd < 0)
    { 
        cout << "failed to get server connection" << endl;
        return false;
    }

    string filename = GetUniqueFilename("testmeta"); 
    Request request;
    UploadRequest* uploadRequest = request.mutable_uploadrequest();
    uploadRequest->set_filename(filename);
    uploadRequest->set_filesize(0);
    uploadRequest->set_ignorelock(true);
    if (metadata.size() > 0)
    {
        *(uploadRequest->mutable_metadata()) = metadata;
    }

    if (!SendMessageToRemote(serverFd, request))
    {
        cout << "couldn't send message to server" << endl;
        close(serverFd);
        return false;
    }

    string responseMessage;
    if (!ReadMessageFromRemote(serverFd, responseMessage))
    {
        cout << "couldn't read message from server" << endl;
        close(serverFd);
        return false;
    }

    UploadResponse response;
    if (!response.ParseFromString(responseMessage))
    {
        std::cout << "error: Failed to parse protobuf message" << endl;
        return false;
    }

    if (response.result() != UploadResponse::SUCCESS)
    {
        std::cout << "error: upload failed" << endl;
        close(serverFd);
        return false;
    }


    Request drequest;

    DownloadRequest* downloadRequest = drequest.mutable_downloadrequest();
    downloadRequest->set_filename(filename);
    downloadRequest->set_enablelock(false);

    if (!SendMessageToRemote(serverFd, drequest))
    {
        cout << "couldn't send message to server" << endl;
        close(serverFd);
        return false;
    }

    if (!ReadMessageFromRemote(serverFd, responseMessage))
    {
        cout << "couldn't read message from server" << endl;
        close(serverFd);
        return false;
    }

    DownloadResponse dresponse;
    if (!dresponse.ParseFromString(responseMessage))
    {
        std::cout << "error: Failed to parse protobuf message" << endl;
        close(serverFd);
        return false;
    }

    if (dresponse.result() != DownloadResponse::SUCCESS)
    {
        std::cout << "error: download failed: response.result() != expectedResult:" << endl;
        close(serverFd);
        return false;
    }

    if (dresponse.filesize() != 0)
    {
        cout << "error: bad filesize: " << dresponse.filesize() << endl;
        close(serverFd);
        return false;
    }

    // check if we got back correct metadata
    if (dresponse.metadata().size() != metadata.size())
    {
        cout << "error: bad metadatasize: " << dresponse.metadata().size() << endl;
        close(serverFd);
        return false;
    }

    for (auto iter : dresponse.metadata())
    {
        if (metadata.at(iter.first) != iter.second)
        {
            cout << "error: received bad metadata. " << metadata.at(iter.first) << " != " << iter.second << endl;
            close(serverFd);
            return false;
        }
    }

    ReleaseConnection(serverFd);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
void TestLockAPIs()
{
    cout << "Test lock APIs" << endl;
    string testlock1 = GetUniqueFilename("testlock1");
    string testlock2 = GetUniqueFilename("testlock2");
    RUNTEST(TestLockAPI(testlock1, true));
    RUNTEST(TestLockAPI(testlock1, false));
    RUNTEST(TestLockAPI(testlock1, false));
    RUNTEST(TestLockAPI(testlock1, false));
    RUNTEST(TestLockAPI(testlock1, false));
    RUNTEST(TestLockAPI(testlock2, true));
    RUNTEST(TestLockAPI(testlock2, false));
    RUNTEST(TestLockAPI(testlock1, false));
    RUNTEST(TestLockAPI(testlock1, false));
    RUNTEST(TestLockAPI(testlock2, false));
}
//---------------------------------------------------------------------------------------------------------------------
void TestUploadDownload()
{
    cout << "Basic upload/download tests" << endl;
    uint64_t hashValue = 0;
    string test1 = GetUniqueFilename("test1");
    string test2 = GetUniqueFilename("test2");
    string test3 = GetUniqueFilename("test3");
    string test4 = GetUniqueFilename("test4");
    string test5 = GetUniqueFilename("test5");
    string test6 = GetUniqueFilename("test6");
    string test7 = GetUniqueFilename("test7");
    string test8 = GetUniqueFilename("test8");
    string test9 = GetUniqueFilename("test9");
    RUNTEST(TestDownloadAPI(test1, 0, 100, true, DownloadResponse::NOT_FOUND_GOT_LOCK));
    RUNTEST(TestDownloadAPI(test1, 0, 100, true, DownloadResponse::NOT_FOUND_ALREADY_LOCKED));

    RUNTEST(TestUploadAPI(test1, 100, false, UploadResponse::SUCCESS, &hashValue));

    RUNTEST(TestDownloadAPI(test1, 0, 100, true, DownloadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI(test1, 0, 100, true, DownloadResponse::SUCCESS, &hashValue));
    
    RUNTEST(TestDownloadAPI(test2, 0, 100, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestDownloadAPI(test2, 0, 100, true, DownloadResponse::NOT_FOUND_GOT_LOCK));
    RUNTEST(TestDownloadAPI(test2, 0, 100, false, DownloadResponse::NOT_FOUND));
    RUNTEST(TestDownloadAPI(test2, 0, 100, true, DownloadResponse::NOT_FOUND_ALREADY_LOCKED));
    RUNTEST(TestUploadAPI(test2, 1024*1024*10 + 1, false, UploadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI(test2, 0, 1024*1024*10 + 1, false, DownloadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI(test2, 0, 1024*1024*10 + 1, true, DownloadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI(test2, 0, 1024*1024*10 + 1, false, DownloadResponse::SUCCESS));

    cout << "Partial download tests" << endl;
    RUNTEST(TestDownloadAPI(test2, 1, 1024*1024*10, false, DownloadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI(test2, 1, 1024*1024*20, false, DownloadResponse::SUCCESS) == false);
    RUNTEST(TestDownloadAPI(test2, 1024*1024*10, 1, false, DownloadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI(test2, 1024*1024*10+1, 0, false, DownloadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI(test2, 1024*1024*10+1, 10, false, DownloadResponse::SUCCESS) == false);
    RUNTEST(TestDownloadAPI(test2, 1024*1024*10+2, 0, false, DownloadResponse::INVALID_OFFSET_LENGTH));
    RUNTEST(TestDownloadAPI(test2, 1024*1024*20, 10, false, DownloadResponse::INVALID_OFFSET_LENGTH));

    RUNTEST(TestDownloadAPI(test1, 0, 100, false, DownloadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI(test1, 0, 100, true, DownloadResponse::SUCCESS));

    RUNTEST(TestUploadAPI(test3, 1, true, UploadResponse::SUCCESS));
    RUNTEST(TestUploadAPI(test3, 10, true, UploadResponse::SUCCESS) == false);

    RUNTEST(TestUploadAPI(test4, 10, false, UploadResponse::SUCCESS) == false);
    RUNTEST(TestUploadAPI(test5, 0, true, UploadResponse::SUCCESS));
    RUNTEST(TestUploadAPI(test5, 10, true, UploadResponse::SUCCESS) == false);

    RUNTEST(TestDownloadAPI(test4, 0, 1024*1024, true, DownloadResponse::NOT_FOUND_GOT_LOCK));
    RUNTEST(TestUploadAPI(test4, 1024*1024, false, UploadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI(test4, 0, 1024*1024, true, DownloadResponse::SUCCESS, &hashValue));

    cout << "test special chars" << endl;
    string testspecial = GetUniqueFilename("test5/dir/%$^&*(^%$#^*////\\\\////");
    RUNTEST(TestDownloadAPI(
        testspecial, 0, 1024*1024+1, true, DownloadResponse::NOT_FOUND_GOT_LOCK));
    RUNTEST(TestUploadAPI(
        testspecial, 1024*1024+1, false, UploadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI(
        testspecial, 0, 1024*1024+1, true, DownloadResponse::SUCCESS, &hashValue));

    cout << "test zero length files" << endl;
    RUNTEST(TestDownloadAPI(test6, 0, 0, true, DownloadResponse::NOT_FOUND_GOT_LOCK));
    RUNTEST(TestUploadAPI(test6, 0, false, UploadResponse::SUCCESS));
    RUNTEST(TestDownloadAPI(test6, 0, 0, true, DownloadResponse::SUCCESS));

    cout << "test long file names" << endl;
    string longFile1 = GetUniqueFilename(string(115, '1'));
    string longFile2 = GetUniqueFilename(string(116, '2'));
    RUNTEST(TestUploadAPI(longFile1, 1024*1024+1, true, UploadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI(longFile1, 0, 1024*1024+1, true, DownloadResponse::SUCCESS, &hashValue));
    RUNTEST(TestUploadAPI(longFile2, 1024*1024+1, true, UploadResponse::SUCCESS) == false);
    RUNTEST(TestDownloadAPI(longFile2, 0, 1024*1024+1, true, DownloadResponse::FILENAME_LIMIT_EXCEEDED));

    cout << "test large file uploads" << endl;
    RUNTEST(TestDownloadAPI(test7, 0, 1024*1024*1024, true, DownloadResponse::NOT_FOUND_GOT_LOCK));
    RUNTEST(TestUploadAPI(test7, 1024*1024*1024, false, UploadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI(test7, 0, 1024*1024*1024, true, DownloadResponse::SUCCESS, &hashValue));
    RUNTEST(TestUploadAPI(test8, (uint64_t)4*1024*1024*1024, true, UploadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI(test8, 0, (uint64_t)4*1024*1024*1024, true, DownloadResponse::SUCCESS, &hashValue));
    RUNTEST(TestDownloadAPI(test8, 0, (uint64_t)4*1024*1024*1024, true, DownloadResponse::SUCCESS, &hashValue));
    RUNTEST(TestUploadAPI(
        test9, (uint64_t)4*1024*1024*1024 + 1, true, UploadResponse::SUCCESS, &hashValue) == false);
    RUNTEST(TestDownloadAPI(
        test9, 0, (uint64_t)4*1024*1024*1024 + 1, false, DownloadResponse::NOT_FOUND, &hashValue));
}
//---------------------------------------------------------------------------------------------------------------------
void TestAttributeCache()
{
    cout << "Test attribute cache" << endl;

    string test1 = GetUniqueFilename("test1");
    string test2 = GetUniqueFilename("test2");
    string test3 = GetUniqueFilename("test3");

    PutAttributeRequest putRequest;
    {
        auto fileAttr = putRequest.add_fileattributes();
        fileAttr->set_filename(test1);
        auto attr = fileAttr->mutable_fileattribute();
        attr->set_isdir(true);
        attr->set_filesize(100);
        attr->set_accessedtime(101);
        attr->set_modifiedtime(102);
    }
    RUNTEST(TestGetAttributeAPI(test1, false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestGetAttributeAPI(test1, true, GetAttributeResponse::NOT_FOUND_GOT_LOCK, false, 0, 0, 0));
    RUNTEST(TestGetAttributeAPI(test1, true, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestGetAttributeAPI(test1, false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestPutAttributeAPI(putRequest, PutAttributeResponse::SUCCESS));
    RUNTEST(TestGetAttributeAPI(test1, false, GetAttributeResponse::SUCCESS, true, 100, 101, 102));

    {
        putRequest.clear_fileattributes();
        auto fileAttr = putRequest.add_fileattributes();
        fileAttr->set_filename(test2);
        auto attr = fileAttr->mutable_fileattribute();
        attr->set_isdir(true);
        attr->set_filesize(0);
        attr->set_accessedtime(201);
        attr->set_modifiedtime(202);

        fileAttr = putRequest.add_fileattributes();
        fileAttr->set_filename(test3);
        attr = fileAttr->mutable_fileattribute();
        attr->set_isdir(false);
        attr->set_filesize(300);
        attr->set_accessedtime(301);
        attr->set_modifiedtime(302);
    }
    RUNTEST(TestGetAttributeAPI(test2, false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestGetAttributeAPI(test3, true, GetAttributeResponse::NOT_FOUND_GOT_LOCK, false, 0, 0, 0));
    RUNTEST(TestGetAttributeAPI(test3, true, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestGetAttributeAPI(test2, false, GetAttributeResponse::NOT_FOUND, false, 0, 0, 0));
    RUNTEST(TestPutAttributeAPI(putRequest, PutAttributeResponse::SUCCESS));
    RUNTEST(TestGetAttributeAPI(test2, true, GetAttributeResponse::SUCCESS, true, 0, 201, 202));
    RUNTEST(TestGetAttributeAPI(test3, false, GetAttributeResponse::SUCCESS, false, 300, 301, 302));
}
//---------------------------------------------------------------------------------------------------------------------
void TestParallelism()
{
    cout << "test parallel lock" << endl;
    RUNTEST(TestParallelLock(200, 5));
    RUNTEST(TestParallelLock(1, 2));
    RUNTEST(TestParallelLock(100, 5));

    cout << "test parallel upload/download" << endl;
    RUNTEST(TestParallelUploadDownload(200, 5));
    RUNTEST(TestParallelUploadDownload(1, 2));
    RUNTEST(TestParallelUploadDownload(500, 10));

    cout << "Test parallel attribute cache" << endl;
    RUNTEST(TestParallelAttributeCache(200, 5));
    RUNTEST(TestParallelAttributeCache(1, 2));
    RUNTEST(TestParallelAttributeCache(500, 10));
}
//---------------------------------------------------------------------------------------------------------------------
void TestCacheMetadata()
{
    cout << "Test cache metadata" << endl;

    // simple metadata
    google::protobuf::Map<string, string> metadata;
    metadata["key1"] = "val1";
    RUNTEST(TestMetadata(metadata));

    // empty metadata
    RUNTEST(TestMetadata(google::protobuf::Map<string, string>()));

    // binary and long metadata
    metadata["key2"] = "val2";
    // long binary data
    metadata["key3"] = string(100*1024, '\4');
    metadata["looooooooooooooooooooooooooooooooooooooongkey4"] = "val4";
    // long text data
    metadata["key5"] = string(100*1024, 'A');
    RUNTEST(TestMetadata(metadata));
}
//---------------------------------------------------------------------------------------------------------------------
void TestDeletes()
{
    cout << "Test delete APIs based on groupids" << endl;

    // Test groupid based delete APIs
    // upload multiple files with different groupids
    string groupid1 = GetUniqueFilename("group1");
    string groupid2 = GetUniqueFilename("group2");
    string deleteFilename1 = GetUniqueFilename("deletefile1");
    string deleteFilename2 = GetUniqueFilename("deletefile2");
    for (int i = 0; i < 100; i++)
    {
        TestUploadAPI(deleteFilename1 + to_string(i), 1024*1024, true, UploadResponse::SUCCESS, nullptr,
            groupid1);
        TestUploadAPI(deleteFilename2 + to_string(i), 1024*1024, true, UploadResponse::SUCCESS, nullptr,
            groupid2);
    }

    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(1), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(50), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(99), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(1), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(50), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(99), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));

    // Delete groupid1, should reflect after a few seconds
    RUNTEST(TestDeleteAPI(groupid1, DeleteResponse::SUCCESS));

    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(1), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(50), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(99), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(1), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(50), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(99), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));

    std::this_thread::sleep_for(std::chrono::seconds(3));

    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(1), 0, 1024*1024, false, DownloadResponse::NOT_FOUND,
        nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(50), 0, 1024*1024, false, DownloadResponse::NOT_FOUND,
        nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(99), 0, 1024*1024, false, DownloadResponse::NOT_FOUND,
        nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(1), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(50), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(99), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));

    // Delete groupid2, should reflect after a few seconds
    RUNTEST(TestDeleteAPI(groupid2, DeleteResponse::SUCCESS));

    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(1), 0, 1024*1024, false, DownloadResponse::NOT_FOUND,
        nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(50), 0, 1024*1024, false, DownloadResponse::NOT_FOUND,
        nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(99), 0, 1024*1024, false, DownloadResponse::NOT_FOUND,
        nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(1), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(50), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(99), 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));

    std::this_thread::sleep_for(std::chrono::seconds(3));

    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(1), 0, 1024*1024, false, DownloadResponse::NOT_FOUND,
        nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(50), 0, 1024*1024, false, DownloadResponse::NOT_FOUND,
        nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename1 + to_string(99), 0, 1024*1024, false, DownloadResponse::NOT_FOUND,
        nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(1), 0, 1024*1024, false, DownloadResponse::NOT_FOUND,
        nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(50), 0, 1024*1024, false, DownloadResponse::NOT_FOUND,
        nullptr));
    RUNTEST(TestDownloadAPI(deleteFilename2 + to_string(99), 0, 1024*1024, false, DownloadResponse::NOT_FOUND,
        nullptr));

    // Should be able to upload the same file immediately after it is deleted
    RUNTEST(TestUploadAPI(deleteFilename1 + to_string(1), 1024*1024, true, UploadResponse::SUCCESS, nullptr,
        groupid1));
    RUNTEST(TestDeleteAPI(groupid1, DeleteResponse::SUCCESS));

    int retryCount = 1;
    while (true)
    {
        if (retryCount % 1000 == 0)
        {
            cout << "retryCount: " << retryCount << ". Still retrying upload after delete" << endl;
            if (TestDownloadAPI(deleteFilename1 + to_string(1), 0, 1024*1024, true,
                DownloadResponse::NOT_FOUND_GOT_LOCK))
            {
                RUNTEST(TestUploadAPI(deleteFilename1 + to_string(1), 1024*1024, true, UploadResponse::SUCCESS,
                    nullptr, groupid1));
                break;
            }
        }
        retryCount++;
    }

    RUNTEST(TestDeleteAPI(groupid1, DeleteResponse::SUCCESS));

    retryCount = 1;
    while (true)
    {
        if (retryCount % 1000 == 0)
        {
            cout << "retryCount: " << retryCount << ". Still retrying upload after delete" << endl;
            if (TestDownloadAPI(deleteFilename1 + to_string(1), 0, 1024*1024, false,
                DownloadResponse::NOT_FOUND))
            {
                RUNTEST(TestUploadAPI(deleteFilename1 + to_string(1), 1024*1024, true, UploadResponse::SUCCESS,
                    nullptr, groupid1));
                break;
            }
        }
        retryCount++;
    }
}
//---------------------------------------------------------------------------------------------------------------------
void TestExpiry()
{
    cout << "Test cache expiry" << endl;
    string file1 = GetUniqueFilename("file1");
    string file2 = GetUniqueFilename("file2");
    string file3 = GetUniqueFilename("file3");
    string file4 = GetUniqueFilename("file4");
    string file5 = GetUniqueFilename("file5");
    string file6 = GetUniqueFilename("file6");
    string file7 = GetUniqueFilename("file7");
    string file8 = GetUniqueFilename("file8");

    TestUploadAPI(file1, 1024*1024, true, UploadResponse::SUCCESS, nullptr, "", 0);
    TestUploadAPI(file2, 1024*1024, true, UploadResponse::SUCCESS, nullptr, "", 1);
    TestUploadAPI(file3, 1024*1024, true, UploadResponse::SUCCESS, nullptr, "", 4);
    TestUploadAPI(file4, 0, true, UploadResponse::SUCCESS, nullptr, "", 4);
    TestUploadAPI(file5, 1024*1024, true, UploadResponse::SUCCESS, nullptr, "", UINT32_MAX);
    TestUploadAPI(file6, 1024*1024, true, UploadResponse::SUCCESS, nullptr, "", 1);
    TestUploadAPI(file7, 1024*1024, true, UploadResponse::SUCCESS, nullptr, "", 1);
    TestUploadAPI(file8, 1024*1024, true, UploadResponse::SUCCESS, nullptr, "", 20);

    RUNTEST(TestDownloadAPI(file1, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file2, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file3, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file4, 0, 0, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file5, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file6, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file7, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file8, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));

    std::this_thread::sleep_for(std::chrono::seconds(3));

    RUNTEST(TestDownloadAPI(file1, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file2, 0, 1024*1024, false, DownloadResponse::NOT_FOUND, nullptr));
    RUNTEST(TestDownloadAPI(file3, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file4, 0, 0, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file5, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file6, 0, 1024*1024, false, DownloadResponse::NOT_FOUND, nullptr));
    RUNTEST(TestDownloadAPI(file7, 0, 1024*1024, false, DownloadResponse::NOT_FOUND, nullptr));
    RUNTEST(TestDownloadAPI(file8, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));

    std::this_thread::sleep_for(std::chrono::seconds(3));

    RUNTEST(TestDownloadAPI(file1, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file2, 0, 1024*1024, false, DownloadResponse::NOT_FOUND, nullptr));
    RUNTEST(TestDownloadAPI(file3, 0, 1024*1024, false, DownloadResponse::NOT_FOUND, nullptr));
    RUNTEST(TestDownloadAPI(file4, 0, 0, false, DownloadResponse::NOT_FOUND, nullptr));
    RUNTEST(TestDownloadAPI(file5, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
    RUNTEST(TestDownloadAPI(file6, 0, 1024*1024, false, DownloadResponse::NOT_FOUND, nullptr));
    RUNTEST(TestDownloadAPI(file7, 0, 1024*1024, false, DownloadResponse::NOT_FOUND, nullptr));
    RUNTEST(TestDownloadAPI(file8, 0, 1024*1024, false, DownloadResponse::SUCCESS, nullptr));
}
//---------------------------------------------------------------------------------------------------------------------
struct IbDev
{
    uint64_t guid;
    uint32_t lid;
    uint8_t port;
    uint8_t link;
    enum ibv_mtu mtu;
    ibv_context* context;
    ibv_pd* pd;
};
IbDev m_ibDevice;

bool IBInit()
{
    int rc = ibv_fork_init();
    if (rc != 0)
    {
        cout << "ibv_fork_init" << endl;
        return false;
    }

    struct ibv_device** devices;
    int numIbDevices = -1;
    devices = ibv_get_device_list(&numIbDevices);
    if (devices == nullptr)
    {
        cout << "ibv_get_device_list" << endl;
        return false;
    }

    bool bDeviceFound = false;
    for(int deviceId = 0; deviceId < numIbDevices && !bDeviceFound; deviceId++)
    {
        struct ibv_context* context = ibv_open_device(devices[deviceId]);
        if (context == nullptr)
        {
            cout << "ibv_open_device" << endl;
            continue;
        }

        struct ibv_device_attr devAttr;
        memset(&devAttr, 0, sizeof(devAttr));
        rc = ibv_query_device(context, &devAttr);
        if (rc != 0)
        {
            cout << "ibv_query_device" << endl;
            ibv_close_device(context);
            continue;
        }

        for(int port = 0; port <= devAttr.phys_port_cnt; port++)
        {
            struct ibv_port_attr portAttr;
            rc = ibv_query_port(context, port, &portAttr);
            if (rc != 0) 
            {
                cout << "ibv_query_port" << endl;
                continue;
            }

            if (portAttr.state != IBV_PORT_ACTIVE)
            {
                cout << "port not active" << endl;
                continue;
            }

            if (portAttr.link_layer != IBV_LINK_LAYER_INFINIBAND)
            {
                cout << "not ib" << endl;
                continue;                        
            };

            m_ibDevice.mtu = portAttr.active_mtu;
            m_ibDevice.lid = portAttr.lid;
            m_ibDevice.guid = devAttr.sys_image_guid;
            m_ibDevice.port = port;
            m_ibDevice.link = portAttr.link_layer;
            m_ibDevice.context = context;
            m_ibDevice.pd = ibv_alloc_pd(context);
            if (m_ibDevice.pd == nullptr)
            {
                cout << "ibv_alloc_pd" << endl;
                continue;
            }

            bDeviceFound = true;
        }

        if (!bDeviceFound)
        {
            ibv_close_device(context);
        }
        else
        {
            break;
        }
    }

    ibv_free_device_list(devices);
    if (!bDeviceFound)
    {
        cout << "no ib devices" << endl;
        return false;
    }

    cout << "Done acquiring local IB device information" << endl;
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool TestIBAPI()
{
    int serverFd = GetConnection();
    if (serverFd < 0)
    { 
        cout << "failed to get server connection" << endl;
	    return false;
    }

    Request request;
    IBInitRequest* ibRequest = request.mutable_ibinitrequest();
    ibRequest->set_clientibport(m_ibDevice.port);
    ibRequest->set_clientlid(m_ibDevice.lid);
    ibRequest->set_clientmtu(m_ibDevice.mtu);
    auto clientQpns = ibRequest->mutable_clientqpn();
    clientQpns->Clear();
    constexpr int numQPsPerConnection = 5;

    auto compEventChannel = ibv_create_comp_channel(m_ibDevice.context);
    if (compEventChannel == nullptr)
    {
        cout << "ibv_create_comp_channel" << endl;
        return false;
    }

    auto cq = ibv_create_cq(m_ibDevice.context, MAX_REQUESTS, NULL, compEventChannel, 0);
    if (cq == nullptr)
    {
        cout << "ibv_create_cq" << endl;
        return false;
    }

    int rc = ibv_req_notify_cq(cq, 0);
    if (rc != 0)
    {
        cout << "ibv_req_notify_cq" << endl;
        return false;
    }

    vector<ibv_qp*> vectorQps;
    for (int qpn = 0; qpn < numQPsPerConnection; qpn++)
    {
        struct ibv_qp_init_attr qpInitAttr;
        memset(&qpInitAttr, 0, sizeof(struct ibv_qp_init_attr));
        qpInitAttr.send_cq = cq;
        qpInitAttr.recv_cq = cq;
        qpInitAttr.srq = nullptr;
        qpInitAttr.qp_type = IBV_QPT_RC;
        qpInitAttr.cap.max_send_wr = 1;
        qpInitAttr.cap.max_recv_wr = 1;
        qpInitAttr.cap.max_send_sge = 1;
        qpInitAttr.cap.max_recv_sge = 1;
        qpInitAttr.cap.max_inline_data = 0;
        ibv_qp *qp = ibv_create_qp(m_ibDevice.pd, &qpInitAttr);
        if (qp == nullptr)
        {
            cout << "ibv_create_qp failed: " << strerror(errno) << endl;
            return false;
        }

        // Transition to INIT state
        struct ibv_qp_attr qpAttr;
        memset(&qpAttr, 0, sizeof(struct ibv_qp_attr));
        qpAttr.qp_state = IBV_QPS_INIT;
        qpAttr.pkey_index = 0;
        qpAttr.port_num = m_ibDevice.port;
        qpAttr.qp_access_flags = 0;
        rc = ibv_modify_qp(qp, &qpAttr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
        if (rc != 0)
        {
            cout << "ibv_modify_qp failed: " << rc << ", " << strerror(errno) << endl;
            return false;
        }
        clientQpns->Add(qp->qp_num);
        vectorQps.push_back(qp);
    }

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

    IBInitResponse response;
    if (!response.ParseFromString(requestMessage))
    {
        std::cout << "Failed to parse protobuf message" << endl;
        close(serverFd);
        return false;
    }

    if (response.result() != IBInitResponse::SUCCESS)
    {
        std::cout << "unexpected response: " << response.result() << endl;
        close(serverFd);
        return false;
    }

    if (response.serverqpn_size() != numQPsPerConnection)
    {
        std::cout << "serverqpn size mismatch: " << numQPsPerConnection << " != " << response.serverqpn_size() << endl;
        close(serverFd);
        return false;
    }

    close(serverFd);
    for (int qpn = 0; qpn < numQPsPerConnection; qpn++)
    {
        // Transition to RTR
        auto qp = vectorQps[qpn];
        struct ibv_qp_attr qpAttr;
        memset(&qpAttr, 0, sizeof(struct ibv_qp_attr));
        qpAttr.qp_state = IBV_QPS_RTR;
        qpAttr.path_mtu = m_ibDevice.mtu;
        qpAttr.dest_qp_num = response.serverqpn().Get(qpn);
        qpAttr.rq_psn = 0;
        qpAttr.max_dest_rd_atomic = 1;
        qpAttr.min_rnr_timer = IB_MIN_RNR_TIMER;
        qpAttr.ah_attr.is_global = 0;
        qpAttr.ah_attr.dlid = response.serverlid();
        qpAttr.ah_attr.sl = 0;
        qpAttr.ah_attr.src_path_bits = 0;
        qpAttr.ah_attr.port_num = response.serveribport();
        rc = ibv_modify_qp(qp, &qpAttr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN
            | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
        if (rc != 0)
        {
            cout << "ibv_modify_qp failed: " << rc << ", " << strerror(errno) << endl;
            return false;
        }

        // Transition to RTS
        memset(&qpAttr, 0, sizeof(struct ibv_qp_attr));
        qpAttr.qp_state = IBV_QPS_RTS;
        qpAttr.timeout = IB_TIMEOUT;
        qpAttr.retry_cnt = IB_RETRY_CNT;
        qpAttr.rnr_retry = IB_RNR_RETRY;
        qpAttr.sq_psn = 0;
        qpAttr.max_rd_atomic = 1;
        rc = ibv_modify_qp(qp, &qpAttr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY
            | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
        if (rc != 0)
        {
            cout << "ibv_modify_qp failed: " << rc << ", " << strerror(errno) << endl;
            return false;
        }
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool TestIBAPIParallel()
{
    std::atomic_bool stop(false);
    std::vector<std::thread *> threads;
    std::atomic_uint_fast64_t totalSuccess(0);
    int parallelism = 10;

    for (int threadId = 0; threadId < parallelism; threadId++)
    {
        auto t = new std::thread([&totalSuccess, &stop]()
        {
            bool bRet = TestIBAPI();
            if (bRet)
            {
                totalSuccess++;
            }
            else
            {
                cout << "TestIBAPI failed" << endl;
            }
        });
        threads.push_back(t);
    }

    for (int i = 0; i < threads.size(); ++i)
    {
        auto t = threads[i];
        t->join();
        delete t;
    }

    if (parallelism != totalSuccess)
    {
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
void TestIB()
{
    cout << "Test IB" << endl;
    IBInit();
    RUNTEST(TestIBAPI());
    RUNTEST(TestIBAPIParallel());
}
//---------------------------------------------------------------------------------------------------------------------
bool TestLiveness()
{
    string file1 = GetUniqueFilename("livenessTest");
    bool result = false;

    result = TestDownloadAPI(file1, 0, 1024 * 1024, false, DownloadResponse::NOT_FOUND, nullptr);
    cout<< "Result for TestDownloadAPI: " << result << endl;

    return result;
}
//---------------------------------------------------------------------------------------------------------------------
/** Invoked to check & run if being used as a cli-application to make requests for integration tests 
 * 
 *  @param isCli [out] true if being used as a cli-application, false otherwise
 *  @return true if command is successful, false otherwise
*/
bool cli(int argc, char* argv[], bool& /*[out]*/ isCli)
{
    std::string command = std::string(argv[1]);
    cout << "CLI Command: " << command << endl;
    isCli = false;

    if(command == "uploadfile")
    {
        isCli = true;
        cout << "uploadfile command" << endl;
        std::string filename = argc > 2 ? std::string(argv[2]) : GetUniqueFilename("filename");
        std::string groupid = argc > 3 ? std::string(argv[3]) : GetUniqueFilename("groupid");
        return !TestUploadAPI(filename, 500, true, UploadResponse::SUCCESS, nullptr, groupid, 0);
    }

    if(command == "uploadmany")
    {
        isCli = true;
        cout << "uploadmany command" << endl;
        std::string groupid = std::string(argv[2]);
        int count = stoi(argv[3]);
        int waitDurationSec = stoi(argv[4]);
        return !TestParallelUploadDownload(count, waitDurationSec, false, groupid);
    }

    if(command == "deletegroup")
    {
        isCli = true;
        cout << "deletegroup command" << endl;
        std::string groupid = argc > 2 ? std::string(argv[2]) : GetUniqueFilename("groupid");
        return !TestDeleteAPI(groupid, DeleteResponse::SUCCESS);
    }

    if(command == "downloadfile")
    {
        isCli = true;
        cout << "downloadfile command" << endl;
        std::string fileName = argc > 2 ? std::string(argv[2]) : GetUniqueFilename("filename");
        return !TestDownloadAPI(fileName, 0, 10, false, DownloadResponse::SUCCESS, nullptr);
    }

    if(command == "uploaddownloadmany")
    {
        isCli = true;
        cout << "uploaddownloadmany command" << endl;
        int count = stoi(argv[2]);
        int waitDurationSec = stoi(argv[3]);
        return !TestParallelUploadDownload(count, waitDurationSec);
    }

    if(command == "measureperf")
    {
        isCli = true;
        cout << "measureperf command" << endl;
        MeasurePerf(16, 10, 200);
        return true;
    }

    cout << "Not a cli command invocation" << endl;
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    char* cacheServerHost = std::getenv("TEST_CACHESERVER_ADDR_HOST");
    if (cacheServerHost != nullptr)
    {
        CacherServer = cacheServerHost;
    }

    char* cacheServerPort = std::getenv("TEST_CACHESERVER_ADDR_PORT");
    if (cacheServerPort != nullptr)
    {
        ServerPort = std::stoi(cacheServerPort);
    }

    // For use by storage_intercept/integration_tests/snapshot_integration_test.sh
    if(argc > 1)
    {
        bool isCli = false;
        bool result = cli(argc, argv, isCli);
        if(isCli)
        {
            return result;
        }
    }

    char* checkLiveness = std::getenv("TEST_CHECK_LIVENESS");
    if(checkLiveness != nullptr)
    {
        if(strcmp(checkLiveness, "true") == 0)
        {
            bool result = TestLiveness();
            cout << "result of Liveness is " << result << endl;
            if(result)
            {
                return 0;
            }
            return 1;
        }
    }

    if(argc > 1)
    {
        return TestLimits();
    }

    TestLockAPIs();
    TestUploadDownload();
    TestAttributeCache();
    TestParallelism();
    TestCacheMetadata();
    TestDeletes();
    TestExpiry();
    TestIB();

    cout << endl << "Total established connections: " << totalConnections << endl;
    cout << "Test complete. Total tests: " << TotalTests << ". Failed: " << TestsFailed << endl;
    
    if (TestsFailed > 0)
    {
        cout << endl << "Ensure cache server is started as "
            "./CacheServer 20000 <folder path on ssd> 100000000000 10000000 1" << endl;
        cout << endl << "Failed tests: " << endl;
        for (string test: FailedTests)
        {
            cout << test << endl;
        }
    }
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------