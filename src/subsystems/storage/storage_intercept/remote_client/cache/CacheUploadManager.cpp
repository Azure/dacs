///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "CacheUploadManager.h"
#include "StorageInterceptConfig.h"
#include "StorageInterceptLogger.h"
#include "StorageInterceptConstants.h"
#include "cache.pb.h"
#include "CacheUtils.h"
#include "ICacheClient.h"
#include <thread>
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace cacheutils;
using namespace std;
using namespace StorageInterceptConstants;
//---------------------------------------------------------------------------------------------------------------------
CacheUploadManager::CacheUploadManager(
    std::shared_ptr<CacheClientImpl> spCacheClientImpl,
    int numUploadThreads,
    uint64_t maxTotalBufferSize)
    : m_stop(false),
      m_uploadBufferMemory(0),
      m_spCacheClientImpl(spCacheClientImpl),
      m_numUploadThreads(numUploadThreads),
      m_maxBufferMemory(maxTotalBufferSize),
      m_keyExpirySeconds(StorageInterceptConfig::GetInstance().GetCacheKeyExpirySeconds()),
      m_keyGroupId(StorageInterceptConfig::GetInstance().GetCacheKeyGroupId()),
      m_enableSyncUpload(StorageInterceptConfig::GetInstance().GetCacheEnableSyncUpload()),
      m_bEnableVersionedCacheKeys(StorageInterceptConfig::GetInstance().IsVersionedCacheKeysEnabled())
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
    // Long enough to hold string representation of std::size_t in base 10 (18446744073709551615)
    // which is 21 characters
    static thread_local char arrFileSizeBuffer[32];
    static thread_local char arrATimeBuffer[32];
    static thread_local char arrMTimeBuffer[32];

    static std::string strTrue("true");
    static std::string strFalse("false");

    LOG_DEBUG("Uploading mountName: {}, file: {}, length: {}, fileAttributes: {} attr-size {}",
              uploadContext->m_strMountName,
              uploadContext->m_strCacheKey,
              uploadContext->m_fileData.get() ? uploadContext->m_fileData->BufferSize : 0,
              uploadContext->m_fileAttributes.get() ? "yes" : "no",
              uploadContext->m_fileAttributes.get() ? uploadContext->m_fileAttributes->size : -1);

    Metadata metadata;
    std::vector<KeyValuePair> metadataEntries;
    if(uploadContext->m_fileAttributes.get())
    {
        metadataEntries.push_back({(char*)CACHE_META_FILE_ISDIR,
            (char*)(uploadContext->m_fileAttributes->isDir ? strTrue.c_str() : strFalse.c_str())});

        snprintf(arrFileSizeBuffer, sizeof(arrFileSizeBuffer), "%ld", uploadContext->m_fileAttributes->size);
        metadataEntries.push_back({(char*)CACHE_META_FILE_SIZE, arrFileSizeBuffer});

        snprintf(arrATimeBuffer, sizeof(arrATimeBuffer), "%ld", uploadContext->m_fileAttributes->aTime.tv_sec);
        metadataEntries.push_back({(char*)CACHE_META_FILE_ATIME, arrATimeBuffer});

        snprintf(arrMTimeBuffer, sizeof(arrMTimeBuffer), "%ld", uploadContext->m_fileAttributes->mTime.tv_sec);
        metadataEntries.push_back({(char*)CACHE_META_FILE_MTIME, arrMTimeBuffer});

        if (!uploadContext->m_fileAttributes->version.empty())
        {
            metadataEntries.push_back({(char*)CACHE_META_FILE_VERSION,
                (char*)uploadContext->m_fileAttributes->version.c_str()});
        }

        metadata.arrKeyValuePairs = metadataEntries.data();
        metadata.nKeyValuePairs = static_cast<int>(metadataEntries.size());
    }
    else
    {
        metadata.arrKeyValuePairs = nullptr;
        metadata.nKeyValuePairs = 0;
    }

    char* bufferToSend = nullptr;
    size_t bufferSizeToSend = 0;
    if(uploadContext->m_fileData.get())
    {
        bufferToSend = uploadContext->m_fileData->Buffer.get();
        bufferSizeToSend = uploadContext->m_fileData->BufferSize;
    }

    // Derive per-file groupId from version if provided, otherwise fall back to global config groupId
    const std::string effectiveGroupId = GetEffectiveGroupId(
        uploadContext->m_strFilePath, uploadContext->m_strVersion);

    // When groupId is set, lifecycle is managed by DeleteGroup(), expiry is not used.
    // The server ignores expiry when groupId is present, so avoid sending both.
    const uint32_t expirySeconds = effectiveGroupId.empty() ? m_keyExpirySeconds : 0;

    auto status = m_spCacheClientImpl->Upload(
        uploadContext->m_strCacheKey.c_str(),
        bufferToSend,
        bufferSizeToSend,
        expirySeconds,
        effectiveGroupId.empty() ? nullptr : effectiveGroupId.c_str(),
        metadata.nKeyValuePairs > 0 ? &metadata : nullptr,
        uploadContext->m_encryptionKey.empty() ? nullptr : uploadContext->m_encryptionKey.c_str());

    return (status == CacheClientStatus::SUCCESS);
}
//---------------------------------------------------------------------------------------------------------------------
void CacheUploadManager::UploadWorker()
{
    LOG_DEBUG("Started UploadWorker");
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

    LOG_DEBUG("Shutdown UploadWorker");
}
//---------------------------------------------------------------------------------------------------------------------
bool CacheUploadManager::Upload(
    const std::string& mountName,
    const std::string& cacheKey,
    std::shared_ptr<const FileData> fileData,
    const std::string& strEncryptionKey,
    std::unique_ptr<::FileAttributes> fileAttributes,
    bool isWrite,
    const std::string& filePath,
    const std::string& version)
{
    bool useSync = m_enableSyncUpload || isWrite; // Writes to cache are always sync

    if (useSync)
    {
        auto uploadContext = make_unique<UploadContext>(
            mountName, cacheKey, fileData, strEncryptionKey,
            move(fileAttributes), filePath, version);
        return StartUpload(move(uploadContext));
    }
    else
    {
        STORAGE_ASSERT(!(fileData.get() && fileData->bIsUserBuffer), "Async upload attempted with user buffer!!");
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
            auto uploadContext = make_unique<UploadContext>(
                mountName, cacheKey, fileData, strEncryptionKey,
                move(fileAttributes), filePath, version);
            m_uploadQueue.push(move(uploadContext));
        }

        m_cvQueue.notify_one();
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
std::string CacheUploadManager::GetEffectiveGroupId(
    const std::string& filePath, const std::string& version) const
{
    if (m_bEnableVersionedCacheKeys && !version.empty())
    {
        return cacheutils::GetGroupIdForFileVersion(filePath, version);
    }
    return m_keyGroupId;
}
//---------------------------------------------------------------------------------------------------------------------
