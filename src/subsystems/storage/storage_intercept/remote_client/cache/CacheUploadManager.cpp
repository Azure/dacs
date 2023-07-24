///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheUploadManager.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptLogger.h"
#include "cache.pb.h"
#include "CacheUtils.h"
#include "ICacheClient.h"
#include <thread>
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace cacheutils;
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
CacheUploadManager::CacheUploadManager(
    ConnectionManager& connectionManager,
    int numUploadThreads,
    uint64_t maxTotalBufferSize)
    : m_stop(false),
      m_uploadBufferMemory(0),
      m_connectionManager(connectionManager),
      m_numUploadThreads(numUploadThreads),
      m_maxBufferMemory(maxTotalBufferSize),
      m_keyExpirySeconds(StorageInterceptConfig::GetInstance().GetCacheKeyExpirySeconds()),
      m_keyGroupId(StorageInterceptConfig::GetInstance().GetCacheKeyGroupId()),
      m_enableSyncUpload(StorageInterceptConfig::GetInstance().GetCacheEnableSyncUpload())
{
    // start worker threads
    if (!m_enableSyncUpload)
    {
        for (int i = 0; i < m_numUploadThreads; ++i)
        {
            auto uploadThread = make_unique<thread>([this]() {
                this->UploadWorker();
            });
            m_vecUploadThread.push_back(move(uploadThread));
        }
    }

    LOG_INFO("Started CacheUploadManager. workers={}, max uploadbuffer={} bytes, expiry={}, groupId={} asyncupload={}",
        m_numUploadThreads,
        m_maxBufferMemory,
        m_keyExpirySeconds,
        m_keyGroupId,
        m_enableSyncUpload ? "false" : "true");
}
//---------------------------------------------------------------------------------------------------------------------
CacheUploadManager::~CacheUploadManager()
{
    if (!m_enableSyncUpload)
    {
        LOG_INFO("Stopping uploader threads");
        m_stop = true;
        m_cvQueue.notify_all();
        for (int i = 0; i < m_numUploadThreads; ++i)
        {
            m_vecUploadThread[i]->join();
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool CacheUploadManager::StartUpload(unique_ptr<UploadContext> uploadContext)
{
    // TODO: Implement retries on failure

    char* bufferToSend = nullptr;
    size_t bufferSizeToSend = 0;
    if (uploadContext->m_fileData.get())
    {
        bufferToSend = uploadContext->m_fileData->Buffer.get();
        bufferSizeToSend = uploadContext->m_fileData->BufferSize;
    }

    Request request;
    UploadRequest* uploadRequest = request.mutable_uploadrequest();
    uploadRequest->set_requestid(cacheutils::GetCorrelationId());
    uploadRequest->set_filename(uploadContext->m_strCacheKey);
    uploadRequest->set_filesize(bufferSizeToSend);
    uploadRequest->set_ignorelock(true);
    uploadRequest->set_expiryseconds(m_keyExpirySeconds);

    LOG_DEBUG_REQID("Uploading mountName: {}, file: {}, length: {}, fileAttributes: {}",
        *uploadRequest,
        uploadContext->m_strMountName,
        uploadContext->m_strCacheKey,
        uploadContext->m_fileData.get() ? uploadContext->m_fileData->BufferSize : 0,
        uploadContext->m_fileAttributes.get() ? "yes" : "no");

    if (!m_keyGroupId.empty())
    {
        uploadRequest->set_groupid(m_keyGroupId);
    }

    if (uploadContext->m_fileAttributes.get())
    {
        auto& fileMetadata = *(uploadRequest->mutable_metadata());
        fileMetadata[CACHE_META_FILE_ISDIR] = uploadContext->m_fileAttributes->isDir ? "true" : "false";
        fileMetadata[CACHE_META_FILE_SIZE] = std::to_string(uploadContext->m_fileAttributes->size);
        fileMetadata[CACHE_META_FILE_ATIME] = std::to_string(uploadContext->m_fileAttributes->aTime.tv_sec);
        fileMetadata[CACHE_META_FILE_MTIME] = std::to_string(uploadContext->m_fileAttributes->mTime.tv_sec);
    }

    if (!AddAuthParams(request))
    {
        LOG_WARN_REQID("failed to create auth request for file: {}", *uploadRequest, uploadContext->m_strCacheKey);
        return false;
    }

    std::unique_ptr<char[]> encBuffer;
    int64_t encLen = -1;
    if (bufferSizeToSend > 0)
    {
        if (!uploadContext->m_encryptionUtil)
        {
            // TODO: replace these with DACS ASSERT_WITH_TRACE_CRITICAL_AND_EXIT
            LOG_CRITICAL_REQID("EncryptionUtil not configured for mount: {}, file: {}",
                *uploadRequest, uploadContext->m_strMountName, uploadContext->m_strCacheKey);
            throw "EncryptionUtil not configured";
        }

        bool bRet = uploadContext->m_encryptionUtil->DoEncrypt(
            uploadRequest, uploadContext->m_fileData, encBuffer, encLen);
        if (!bRet)
        {
            LOG_WARN_REQID("couldn't encrypt data to send to server, file: {}", *uploadRequest, uploadContext->m_strCacheKey);
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
        LOG_WARN_REQID("couldn't get server connection, ignoring upload for file: {}", *uploadRequest, uploadContext->m_strCacheKey);
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
        LOG_WARN_REQID("couldn't send request, ignoring upload for file: {}", *uploadRequest, uploadContext->m_strCacheKey);
        m_connectionManager.ReleaseConnection(uploadContext->m_serverIndex, move(serverConnection), true);
        return false;
    }

    if (bufferSizeToSend > 0)
    {
        bool bRet = cacheutils::WriteAllFinal(serverConnection.get(), bufferToSend, bufferSizeToSend);
        if (!bRet)
        {
            LOG_WARN_REQID("couldn't complete write to server, file: {}", *uploadRequest, uploadContext->m_strCacheKey);
            m_connectionManager.ReleaseConnection(uploadContext->m_serverIndex, move(serverConnection), true);
            return false;
        }
    }

    string strResponse;
    if (!ReadMessageFromRemote(serverConnection.get(), strResponse))
    {
        LOG_WARN_REQID("couldn't read response from server. file: {}", *uploadRequest, uploadContext->m_strCacheKey);
        m_connectionManager.ReleaseConnection(uploadContext->m_serverIndex, move(serverConnection), true);
        return false;
    }

    UploadResponse response;
    if (!response.ParseFromString(strResponse))
    {
        LOG_WARN_REQID("couldn't parse response from server. file: {}", *uploadRequest, uploadContext->m_strCacheKey);
        m_connectionManager.ReleaseConnection(uploadContext->m_serverIndex, move(serverConnection), true);
        return false;
    }

    bool status = false;
    if (response.result() == UploadResponse::SUCCESS)
    {
        LOG_DEBUG_REQID("Uploaded to server successfully. file: {}", *uploadRequest, uploadContext->m_strCacheKey);
        status = true;
    }
    else
    {
        LOG_WARN_REQID("Upload to server failed. file: {}, error: {}", *uploadRequest, uploadContext->m_strCacheKey, response.result());
    }

    m_connectionManager.ReleaseConnection(uploadContext->m_serverIndex, move(serverConnection), false);
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
        if (m_uploadQueue.size() > 0)
        {
            uploadContext = move(m_uploadQueue.front());
            m_uploadQueue.pop();
        }

        lock.unlock();

        if (uploadContext != nullptr)
        {
            size_t bufferSizeToSend = uploadContext->m_fileData.get() ? uploadContext->m_fileData->BufferSize : 0;
            StartUpload(move(uploadContext));
            m_uploadBufferMemory -= bufferSizeToSend;
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
    EncryptionUtil* encryptionUtil,
    std::unique_ptr<::FileAttributes> fileAttributes)
{
    if (m_enableSyncUpload)
    {
        auto uploadContext = make_unique<UploadContext>(mountName, serverIndex, cacheKey, fileData, encryptionUtil,
            move(fileAttributes));
        return StartUpload(move(uploadContext));
    }
    else
    {
        size_t bufferSizeToSend = fileData.get() ? fileData->BufferSize : 0;
        {
            lock_guard lock(m_mutexQueue);
            if ((m_uploadBufferMemory + bufferSizeToSend) > m_maxBufferMemory)
            {
                LOG_RARELY(10, WARN, "Reached upload size limit, skipping. cacheKey={}, size={}, upload pending={}",
                    cacheKey, bufferSizeToSend, m_uploadBufferMemory);
                return false;
            }

            m_uploadBufferMemory += bufferSizeToSend;
            auto uploadContext = make_unique<UploadContext>(mountName, serverIndex, cacheKey, fileData,
                encryptionUtil, move(fileAttributes));
            m_uploadQueue.push(move(uploadContext));
        }

        m_cvQueue.notify_one();
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
