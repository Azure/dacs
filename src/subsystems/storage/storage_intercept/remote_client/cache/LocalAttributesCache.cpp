///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "LocalAttributesCache.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptUtils.h"
#include "cache.pb.h"
#include "CacheUtils.h"
#include <condition_variable>
#include <thread>
#include <dirent.h>
#include <chrono>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cacheutils;
//---------------------------------------------------------------------------------------------------------------------
LocalAttributesCache::LocalAttributesCache()
: m_stop(false)
{
    int serverPort = StorageInterceptConfig::GetInstance().GetAttributesCacheServerPort();
    m_spConnectionManager = make_unique<ConnectionManager>(serverPort, true, vector<string>());

    // start worker thread
    m_uploadThread = make_unique<thread>([this]() {
        this->UploadWorker();
    });

    LOG_INFO("Using LocalAttributesCache for caching remote file properties. server port={}",
        serverPort);
}
//---------------------------------------------------------------------------------------------------------------------
LocalAttributesCache::~LocalAttributesCache()
{
    m_stop = true;
    m_cvQueue.notify_all();
    m_uploadThread->join();
}
//---------------------------------------------------------------------------------------------------------------------
CacheStatus LocalAttributesCache::Get(const std::string& path, FileAttributes& fileAttributes)
{
    CacheStatus cacheStatus(CacheStatus::INTERNAL_ERROR);
    cache::Request request;
    cache::GetAttributeRequest* getAttributeRequest = request.mutable_getattributerequest();
    getAttributeRequest->set_requestid(cacheutils::GetCorrelationId());
    getAttributeRequest->set_filename(path);
    getAttributeRequest->set_enablelock(true);
    LOG_INFO_REQID("Creating GetAttributeRequest for path: {}", *getAttributeRequest, path);

    auto serverConnection = move(m_spConnectionManager->GetConnection(0));
    if (serverConnection.get() == nullptr)
    {
        LOG_WARN_REQID("couldn't get server connection", *getAttributeRequest);
        return cacheStatus;
    }

    if (!SendMessageToRemoteFinal(serverConnection.get(), request))
    {
        LOG_WARN_REQID("couldn't send message to server", *getAttributeRequest);
        m_spConnectionManager->ReleaseConnection(0, move(serverConnection), true);
        return cacheStatus;
    }

    string responseMessage;
    if (!ReadMessageFromRemote(serverConnection.get(), responseMessage))
    {
        LOG_WARN_REQID("couldn't read message from server", *getAttributeRequest);
        m_spConnectionManager->ReleaseConnection(0, move(serverConnection), true);
        return cacheStatus;
    }

    cache::GetAttributeResponse response;
    if (!response.ParseFromString(responseMessage))
    {
        LOG_WARN_REQID("error: Failed to parse protobuf message", *getAttributeRequest);
        m_spConnectionManager->ReleaseConnection(0, move(serverConnection), true);
        return cacheStatus;
    }

    LOG_DEBUG_REQID("GetAttributeResponse from server: {}, key: {}, filesize: {}",
         *getAttributeRequest, response.result(), path, response.fileattribute().filesize());
    if (response.result() == cache::GetAttributeResponse::SUCCESS)
    {
        fileAttributes.isDir = response.fileattribute().isdir();
        fileAttributes.size = response.fileattribute().filesize();
        fileAttributes.aTime.tv_sec = response.fileattribute().accessedtime();
        fileAttributes.mTime.tv_sec = response.fileattribute().modifiedtime();
        fileAttributes.cTime.tv_sec = response.fileattribute().modifiedtime();
        cacheStatus = CacheStatus::SUCCESS;
    }
    else if (response.result() == cache::GetAttributeResponse::NOT_FOUND_GOT_LOCK)
    {
        cacheStatus = CacheStatus::NOTFOUND_LOCKED;
    }
    else if (response.result() == cache::GetAttributeResponse::NOT_FOUND)
    {
        cacheStatus = CacheStatus::NOTFOUND;
    }
    else if (response.result() == cache::GetAttributeResponse::AUTH_FAILED)
    {
        LOG_WARN_REQID("Server returned auth failure for key: {}", *getAttributeRequest, path);
        cacheStatus = CacheStatus::AUTH_FAILED;
    }
    else
    {
        LOG_WARN_REQID("Server returned internal failure for key: {}", *getAttributeRequest, path);
        cacheStatus = CacheStatus::INTERNAL_ERROR;
    }

    m_spConnectionManager->ReleaseConnection(0, move(serverConnection), false);
    return cacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
void LocalAttributesCache::Put(const std::string& path, const FileAttributes& fileAttributes)
{
    // Update the entry to latest value
    {
        lock_guard lock(m_mutexQueue);
        auto uploadContext = make_unique<UploadContext>(path, fileAttributes);
        m_uploadQueue.push(move(uploadContext));
    }

    m_cvQueue.notify_one();
}
//---------------------------------------------------------------------------------------------------------------------
void LocalAttributesCache::PutAll(
    const std::string& pathPrefix,
    const std::vector<DirectoryEntry>& dirEntries,
    const size_t startIndex)
{
    int numEntries = dirEntries.size() - startIndex;
    if (numEntries <= 0)
    {
        return;
    }

    LOG_DEBUG("Adding {} entries to attribute cache", numEntries);
    {
        lock_guard lock(m_mutexQueue);

        // Update the new entries to latest value
        for (size_t index = startIndex; index < dirEntries.size(); index++)
        {
            const DirectoryEntry& dirEntry = dirEntries[index];
            FileAttributes fileAttributes;
            fileAttributes.isDir = (dirEntry.d_type == DT_DIR);
            fileAttributes.size = dirEntry.d_size;
            fileAttributes.mTime.tv_sec = dirEntry.d_mtime;
            fileAttributes.aTime.tv_sec = dirEntry.d_atime;
            fileAttributes.cTime = fileAttributes.mTime;
            auto uploadContext = make_unique<UploadContext>(
                StorageInterceptUtils::CombinePath(pathPrefix, dirEntry.d_name), fileAttributes);
            m_uploadQueue.push(move(uploadContext));
        }
    }

    m_cvQueue.notify_one();
}
//---------------------------------------------------------------------------------------------------------------------
void LocalAttributesCache::UploadWorker()
{
    LOG_INFO("Started LocalAttributesCache::UploadWorker");
    while (!m_stop)
    {
        std::unique_lock<std::mutex> lock(m_mutexQueue);
        bool bRet = m_cvQueue.wait_for(
            lock,
            std::chrono::seconds(QUEUE_MAX_WAIT_TIME_SECONDS),
            [this]
            {
                return this->m_stop || (this->m_uploadQueue.size() > COMBINE_QUEUE_ENTRIES);
            });
        if (m_stop)
        {
            break;
        }

        std::queue<std::unique_ptr<UploadContext>> uploadQueue;
        if (m_uploadQueue.size() > 0) {
            uploadQueue.swap(m_uploadQueue);
        }
        lock.unlock();

        if (uploadQueue.size() > 0) {
            StartUpload(uploadQueue);
        }
    }
    LOG_INFO("Shutdown UploadWorker");
}
//---------------------------------------------------------------------------------------------------------------------
bool LocalAttributesCache::StartUpload(std::queue<std::unique_ptr<UploadContext>>& uploadQueue)
{
    // TODO: Implement retries on failure
    LOG_DEBUG("Uploading attributes, num entries: {}", uploadQueue.size());

    cache::Request request;
    cache::PutAttributeRequest* putAttributeRequest = request.mutable_putattributerequest();
    putAttributeRequest->set_requestid(cacheutils::GetCorrelationId());
    while (!uploadQueue.empty())
    {
        auto queueEntry = move(uploadQueue.front());
        uploadQueue.pop();
        auto fileAttr = putAttributeRequest->add_fileattributes();
        fileAttr->set_filename(queueEntry->m_strCacheKey);
        auto attr = fileAttr->mutable_fileattribute();
        attr->set_isdir(queueEntry->m_fileAttributes.isDir);
        attr->set_filesize(queueEntry->m_fileAttributes.size);
        attr->set_accessedtime(queueEntry->m_fileAttributes.aTime.tv_sec);
        attr->set_modifiedtime(queueEntry->m_fileAttributes.mTime.tv_sec);
    }
    LOG_INFO_REQID("Creating PutAttributeRequest with {} entries", *putAttributeRequest, putAttributeRequest->fileattributes_size());

    auto serverConnection = move(m_spConnectionManager->GetConnection(0));
    if (serverConnection.get() == nullptr)
    {
        LOG_WARN_REQID("couldn't get server connection, ignoring upload", *putAttributeRequest);
        return false;
    }

    if (!SendMessageToRemoteFinal(serverConnection.get(), request))
    {
        LOG_WARN_REQID("couldn't send request, ignoring upload", *putAttributeRequest);
        m_spConnectionManager->ReleaseConnection(0, move(serverConnection), true);
        return false;
    }

    LOG_DEBUG_REQID("PutAttributeRequest request sent to server", *putAttributeRequest);
    string strResponse;
    if (!ReadMessageFromRemote(serverConnection.get(), strResponse))
    {
        LOG_WARN_REQID("couldn't read response from server", *putAttributeRequest);
        m_spConnectionManager->ReleaseConnection(0, move(serverConnection), true);
        return false;
    }

    cache::GetAttributeResponse response;
    if (!response.ParseFromString(strResponse))
    {
        LOG_WARN_REQID("couldn't parse GetAttributeResponse from server", *putAttributeRequest);
        m_spConnectionManager->ReleaseConnection(0, move(serverConnection), true);
        return false;
    }

    if (response.result() == cache::GetAttributeResponse::SUCCESS)
    {
        LOG_DEBUG_REQID("Uploaded attributes to server successfully", *putAttributeRequest);
    }
    else
    {
        LOG_WARN_REQID("Upload attributes to server failed, result: {}",  *putAttributeRequest, response.result());
        // TODO: log entries which failed
    }
    m_spConnectionManager->ReleaseConnection(0, move(serverConnection), false);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------