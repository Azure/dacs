///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheUploadManager.h"
#include "StorageInterceptLogger.h"
#include "cache.pb.h"
#include "CacheUtils.h"
#include "ICacheClient.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace std;
using namespace cacheutils;
//---------------------------------------------------------------------------------------------------------------------
CacheUploadManager::CacheUploadManager(
    ConnectionManager& connectionManager,
    int numUploadThreads,
    uint64_t maxTotalBufferSize)
    : m_stop(false),
      m_uploadBufferMemory(0),
      m_connectionManager(connectionManager),
      m_numUploadThreads(numUploadThreads),
      m_maxBufferMemory(maxTotalBufferSize)
{
    // start worker threads
    for (int i = 0; i < m_numUploadThreads; ++i)
    {
        auto uploadThread = make_unique<thread>([this]() {
            this->UploadWorker();
        });
        m_vecUploadThread.push_back(move(uploadThread));
    }

    LOG_INFO("Started CacheUploadManager. Num workers: {}, max upload buffer size: {} bytes",
        m_numUploadThreads, m_maxBufferMemory);
}
//---------------------------------------------------------------------------------------------------------------------
CacheUploadManager::~CacheUploadManager()
{
    LOG_INFO("Stopping uploader threads");
    m_stop = true;
    m_cvQueue.notify_all();
    for (int i = 0; i < m_numUploadThreads; ++i)
    {
        m_vecUploadThread[i]->join();
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool CacheUploadManager::StartUpload(unique_ptr<UploadContext> uploadContext)
{
    // TODO: Implement retries on failure
    LOG_DEBUG("Uploading mountName: {}, file: {}, length: {}",
        uploadContext->m_strMountName,
        uploadContext->m_strCacheKey,
        uploadContext->m_fileData->BufferSize);

    char* bufferToSend = uploadContext->m_fileData->Buffer.get();
    size_t bufferSizeToSend = uploadContext->m_fileData->BufferSize;

    Request request;
    UploadRequest* uploadRequest = request.mutable_uploadrequest();
    uploadRequest->set_filename(uploadContext->m_strCacheKey);
    uploadRequest->set_filesize(uploadContext->m_fileData->BufferSize);
    uploadRequest->set_ignorelock(false);

    if (!AddAuthParams(request))
    {
        LOG_WARN("failed to create auth request for file: {}", uploadContext->m_strCacheKey);
        m_uploadBufferMemory -= uploadContext->m_fileData->BufferSize;
        return false;
    }

    std::unique_ptr<char[]> encBuffer;
    int64_t encLen = -1;
    if (bufferSizeToSend > 0)
    {
        bool bRet = uploadContext->m_encryptionUtil.DoEncrypt(uploadRequest, uploadContext->m_fileData, encBuffer, encLen);
        if (!bRet)
        {
            LOG_WARN("couldn't encrypt data to send to server, file: {}", uploadContext->m_strCacheKey);
            m_uploadBufferMemory -= uploadContext->m_fileData->BufferSize;
            return false;
        }
        
        // encLen will be updated if encrypted data needs to be used 
        if (encLen == bufferSizeToSend)
        {
            // Data encrypted to new buffer, replace original send buffer with the encrypted one
            bufferToSend = encBuffer.get();
        }
    }

    auto serverConnection = move(m_connectionManager.GetConnection(uploadContext->m_serverIndex));
    if (serverConnection.get() == nullptr)
    {
        LOG_WARN("couldn't get server connection, ignoring upload for file: {}", uploadContext->m_strCacheKey);
        m_uploadBufferMemory -= uploadContext->m_fileData->BufferSize;
        return false;
    }

    bool bRet = false;
    if (bufferSizeToSend > 0)
    {
        bRet = SendMessageToRemote(serverConnection.get(), request);
    }
    else
    {
        bRet = SendMessageToRemoteFinal(serverConnection.get(), request);
    }

    if (!bRet)
    {
        LOG_WARN("couldn't send request, ignoring upload for file: {}", uploadContext->m_strCacheKey);
        m_connectionManager.ReleaseConnection(uploadContext->m_serverIndex, move(serverConnection), true);
        m_uploadBufferMemory -= uploadContext->m_fileData->BufferSize;
        return false;
    }

    if (bufferSizeToSend > 0)
    {
        bool bRet = cacheutils::WriteAllFinal(serverConnection.get(), bufferToSend, bufferSizeToSend);
        if (!bRet)
        {
            LOG_WARN("couldn't complete write to server, file: {}", uploadContext->m_strCacheKey);
            m_connectionManager.ReleaseConnection(uploadContext->m_serverIndex, move(serverConnection), true);
            m_uploadBufferMemory -= uploadContext->m_fileData->BufferSize;
            return false;
        }
    }

    string strResponse;
    if (!ReadMessageFromRemote(serverConnection.get(), strResponse))
    {
        LOG_WARN("couldn't read response from server. file: {}", uploadContext->m_strCacheKey);
        m_connectionManager.ReleaseConnection(uploadContext->m_serverIndex, move(serverConnection), true);
        m_uploadBufferMemory -= uploadContext->m_fileData->BufferSize;
        return false;
    }

    UploadResponse response;
    if (!response.ParseFromString(strResponse))
    {
        LOG_WARN("couldn't parse response from server. file: {}", uploadContext->m_strCacheKey);
        m_connectionManager.ReleaseConnection(uploadContext->m_serverIndex, move(serverConnection), true);
        m_uploadBufferMemory -= uploadContext->m_fileData->BufferSize;
        return false;
    }

    bool status = false;
    if (response.result() == UploadResponse::SUCCESS)
    {
        LOG_DEBUG("Uploaded to server successfully. file: {}", uploadContext->m_strCacheKey);
        status = true;
    }
    else
    {
        LOG_WARN("Upload to server failed. file: {}, error: {}", uploadContext->m_strCacheKey, response.result());
    }
    m_connectionManager.ReleaseConnection(uploadContext->m_serverIndex, move(serverConnection), false);
    m_uploadBufferMemory -= uploadContext->m_fileData->BufferSize;
    return status;
}
//---------------------------------------------------------------------------------------------------------------------
void CacheUploadManager::UploadWorker()
{
    LOG_INFO("Started UploadWorker");
    while (!m_stop)
    {
        std::unique_lock<std::mutex> lock(m_mutexQueue);
        m_cvQueue.wait(lock,
            [this]
            {
                return this->m_stop || (this->m_uploadQueue.size() > 0);
            });
        if (m_stop)
        {
            break;
        }

        unique_ptr<UploadContext> uploadContext;
        if (m_uploadQueue.size() > 0) {
            uploadContext = move(m_uploadQueue.front());
            m_uploadQueue.pop();
        }
        lock.unlock();

        if (uploadContext != nullptr) {
            StartUpload(move(uploadContext));
        }
    }
    LOG_INFO("Shutdown UploadWorker");
}
//---------------------------------------------------------------------------------------------------------------------
bool CacheUploadManager::Upload(
    const std::string& mountName,
    int serverIndex,
    const std::string& cacheKey,
    std::shared_ptr<const FileReadData> fileData,
    EncryptionUtil& encryptionUtil)
{
    {
        lock_guard lock(m_mutexQueue);
        if ((m_uploadBufferMemory + fileData->BufferSize) > m_maxBufferMemory)
        {
            LOG_CRITICAL("Reached upload size limit. cacheKey: {}, size: {}, upload bytes pending: {}",
                cacheKey, fileData->BufferSize, m_uploadBufferMemory);
            // TODO: need to reset upload lock and retry again on next cache miss, for now hoping for the best
        }

        m_uploadBufferMemory += fileData->BufferSize;
        auto uploadContext = make_unique<UploadContext>(mountName, serverIndex, cacheKey, fileData, encryptionUtil);
        m_uploadQueue.push(move(uploadContext));
    }

    m_cvQueue.notify_one();
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
