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

    LOG_INFO("Started CacheUploadManager. Num workers: {}", m_numUploadThreads);
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
    LOG_DEBUG("Uploading file: {}, length: {}",
        uploadContext->m_strCacheKey, uploadContext->m_fileData->BufferSize);

    Request request;
    UploadRequest* uploadRequest = request.mutable_uploadrequest();
    uploadRequest->set_filename(uploadContext->m_strCacheKey);
    uploadRequest->set_filesize(uploadContext->m_fileData->BufferSize);    
    uploadRequest->set_ignorelock(false);

    int serverFd = m_connectionManager.GetFd(uploadContext->m_serverIndex);
    if (serverFd < 0)
    {
        LOG_WARN("couldn't get server connection, ignoring upload for file: {}", uploadContext->m_strCacheKey);
        m_uploadBufferMemory -= uploadContext->m_fileData->BufferSize;
        return false;
    }

    if (!SendMessageToRemote(serverFd, request))
    {
        LOG_WARN("couldn't send request, ignoring upload for file: {}", uploadContext->m_strCacheKey);
        m_connectionManager.ReleaseFd(uploadContext->m_serverIndex, serverFd, true);
        m_uploadBufferMemory -= uploadContext->m_fileData->BufferSize;
        return false;
    }

    LOG_DEBUG("upload request sent to server");
    if (uploadContext->m_fileData->BufferSize > 0)
    {
        uint64_t totalWritten = 0;
        while (totalWritten < uploadContext->m_fileData->BufferSize)
        {
            int bufferLen = uploadContext->m_fileData->BufferSize - totalWritten;
            int tempWrite;
            if ((tempWrite = TimedWrite(
                serverFd, 
                static_cast<const char*>(uploadContext->m_fileData->Buffer.get()) + totalWritten,
                bufferLen)) > 0)
            {
                totalWritten += tempWrite;
            }
            else 
            {
                LOG_WARN("couldn't complete write to server. wrote: {}, error: {}", totalWritten, tempWrite);
                m_connectionManager.ReleaseFd(uploadContext->m_serverIndex, serverFd, true);
                m_uploadBufferMemory -= uploadContext->m_fileData->BufferSize;
                return false;
            }
        }
    }

    LOG_DEBUG("uploaded file to server");
    string strResponse;
    if (!ReadMessageFromRemote(serverFd, strResponse))
    {
        LOG_WARN("couldn't read response from server. file: {}", uploadContext->m_strCacheKey);
        m_connectionManager.ReleaseFd(uploadContext->m_serverIndex, serverFd, true);
        m_uploadBufferMemory -= uploadContext->m_fileData->BufferSize;
        return false;
    }

    UploadResponse response;
    if (!response.ParseFromString(strResponse))
    {
        LOG_WARN("couldn't parse response from server. file: {}", uploadContext->m_strCacheKey);
        m_connectionManager.ReleaseFd(uploadContext->m_serverIndex, serverFd, true);
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
    m_connectionManager.ReleaseFd(uploadContext->m_serverIndex, serverFd, false);
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
    int serverIndex,
    const std::string& cacheKey,
    std::shared_ptr<const FileReadData> fileData)
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
        auto uploadContext = make_unique<UploadContext>(serverIndex, cacheKey, fileData);
        m_uploadQueue.push(move(uploadContext));
    }

    m_cvQueue.notify_one();
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
