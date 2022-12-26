///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "LocalFSCacheClient.h"
#include "StorageInterceptLogger.h"
#include "StorageDlsymWrapper.h"
#include "StorageInterceptConfig.h"
#include "CacheUtils.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cacheutils;
//---------------------------------------------------------------------------------------------------------------------
LocalFSCacheClient::LocalFSCacheClient()
: m_strLocalCachePath(StorageInterceptConfig::GetInstance().GetCacheLocalFSCachePath()),
  m_strLocalCacheTempPath(StorageInterceptConfig::GetInstance().GetCacheLocalFSCacheStagingPath()),
  m_useODirectFlag(StorageInterceptConfig::GetInstance().GetCacheLocalFSEnableIODirect()),
  m_numUploadThreads(StorageInterceptConfig::GetInstance().GetCacheNumUploadThreadsPerProcess()),
  m_uploadBufferMemory(0),
  m_maxUploadBufferMemory(
    StorageInterceptConfig::GetInstance().GetCacheMaxUploadBufferSizePerProcessGB() * 1024ULL * 1024 * 1024),
  m_uploadBufferMemoryWarnThreshold(
    StorageInterceptConfig::GetInstance().GetCacheLocalFSMemoryWarnThresholdGB() * 1024ULL * 1024 * 1024),
  m_stop(false),
  m_lastLoggedTime(0)
{
    // Create local cache path if not exists
    if (::mkdir(m_strLocalCachePath.c_str(), 0777) != 0 && errno != EEXIST)
    {
        LOG_CRITICAL("Cannot create local directory={}", m_strLocalCachePath);
        throw ("Cannot create local directory=" + m_strLocalCachePath);
    }

    if (::mkdir(m_strLocalCacheTempPath.c_str(), 0777) != 0 && errno != EEXIST)
    {
        LOG_CRITICAL("Cannot create local directory={}", m_strLocalCacheTempPath);
        throw ("Cannot create local directory=" + m_strLocalCacheTempPath);
    }

    // start worker threads
    for (int i = 0; i < m_numUploadThreads; ++i)
    {
        auto uploadThread = make_unique<thread>([this]() {
            this->UploadWorker();
        });
        m_vecUploadThread.push_back(move(uploadThread));
    }

    // max size of 0 means unlimited
    if (m_maxUploadBufferMemory == 0)
    {
        m_maxUploadBufferMemory = std::numeric_limits<uint64_t>::max();
    }

    LOG_INFO("Configured LocalFSCacheClient path={}, Temp={}, Uploaders={}, MemWarn bytes={} MemLimit bytes={}",
        m_strLocalCachePath,
        m_strLocalCacheTempPath,
        m_numUploadThreads,
        m_uploadBufferMemoryWarnThreshold,
        m_maxUploadBufferMemory);
}
//---------------------------------------------------------------------------------------------------------------------
LocalFSCacheClient::~LocalFSCacheClient()
{
    LOG_INFO("Stopping localFS uploader threads");
    m_stop = true;
    m_cvQueue.notify_all();
    for (int i = 0; i < m_numUploadThreads; ++i)
    {
        m_vecUploadThread[i]->join();
    }
}
//---------------------------------------------------------------------------------------------------------------------
CacheStatus LocalFSCacheClient::DownloadSync(
    const std::string& mountName,
    const std::string& filePath,
    off64_t fileOffset,
    uint64_t startOffset,
    uint64_t length,
    char *buffer,
    uint64_t& readLength)
{
    CacheStatus cacheStatus = CacheStatus::INTERNAL_ERROR;
    const std::string cacheKey = GetCacheKey(filePath, fileOffset);
    const string localFilePath = m_strLocalCachePath + "/" + cacheKey;

    if (startOffset > 0)
    {
        LOG_CRITICAL("non zero offset not supported, mountName={}, file={}, offset={}",
            mountName, localFilePath, startOffset);
        throw "non zero offset not supported";
    }

    const int fileFD = StorageDlsymWrapper::open(localFilePath.c_str(), O_RDONLY, 0777);
    if (fileFD >= 0)
    {
        uint64_t totalRead = 0;
        while (totalRead < length)
        {
            const int tempRead = StorageDlsymWrapper::read(fileFD, buffer + totalRead, length - totalRead);
            if (tempRead > 0)
            {
                totalRead += tempRead;
            }
            else if (tempRead == 0)
            {
                LOG_DEBUG("Reached EOF for mountName={}, file={}", mountName, localFilePath);
                break;
            }
            else
            {
                LOG_WARN("Failed to read() from mountName={}, file={}, errno={}:{}",
                    mountName, localFilePath, errno, strerror(errno))
                StorageDlsymWrapper::close(fileFD);
                return cacheStatus;
            }
        }

        readLength = totalRead;
        StorageDlsymWrapper::close(fileFD);
        cacheStatus = CacheStatus::SUCCESS;
    }
    else
    {
        // Return locked status so clients are notified to trigger an upload.
        // Multiple clients might attempt to upload same file, its guarded in upload path to prevent any side effects
        LOG_DEBUG("File not found in cache mountName={}, file={}", mountName, localFilePath);
        cacheStatus = CacheStatus::NOTFOUND_LOCKED;
    }

    return cacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
bool LocalFSCacheClient::UploadSync(std::unique_ptr<UploadContext> uploadContext)
{
    const string tmpFilePath = m_strLocalCacheTempPath + "/" + uploadContext->m_strCacheKey;

    // Using O_EXCL to prevent parallel uploads to same file
    int openFlags = O_WRONLY | O_CREAT | O_EXCL;
    if (m_useODirectFlag &&
        uploadContext->m_fileData->BufferSize % uploadContext->m_fileData->BufferAlignmentSize == 0)
    {
        // We can use O_DIRECT only if buffer size is aligned to filesystem's logical block size
        openFlags |= O_DIRECT;
    }

    const int fileFD = StorageDlsymWrapper::open(tmpFilePath.c_str(), openFlags, 0644);
    if (fileFD < 0)
    {
        // TODO: Handle cases where there is left over file from previous run.
        //       For now we should ensure this externally by clearing the temp path before application starts.
        if (errno == EEXIST)
        {
            LOG_WARN("File already opened for upload={}", tmpFilePath);
        }
        else
        {
            LOG_ERROR("Cannot open file for writing={} fd={} openflags={} errno={}:{}",
                tmpFilePath, fileFD, openFlags, errno, strerror(errno));
        }

        return false;        
    }

    uint64_t totalWritten = 0;
    while (totalWritten < uploadContext->m_fileData->BufferSize)
    {
        const int bufferLen = uploadContext->m_fileData->BufferSize - totalWritten;
        const int tempWrite = write(
            fileFD, 
            static_cast<const char*>(uploadContext->m_fileData->Buffer.get()) + totalWritten,
            bufferLen);
        if (tempWrite > 0)
        {
            totalWritten += tempWrite;
        }
        else 
        {
            LOG_ERROR("couldn't complete write to file={} fd={} openflags={} wrote={} actual length={} errno={}:{}",
                tmpFilePath, fileFD, openFlags, totalWritten, uploadContext->m_fileData->BufferSize, errno,
                strerror(errno));
            StorageDlsymWrapper::close(fileFD);
            remove(tmpFilePath.c_str());
            return false;
        }
    }

    StorageDlsymWrapper::close(fileFD);

    // Now rename file to final destination
    const string destFilename = m_strLocalCachePath + "/" + uploadContext->m_strCacheKey;
    int ret = rename(tmpFilePath.c_str(), destFilename.c_str());
    if (ret != 0)
    {
        LOG_WARN("Failed to rename file from {} to {}. errno={}:{}",
            tmpFilePath, destFilename, errno, strerror(errno));
        remove(tmpFilePath.c_str());
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
void LocalFSCacheClient::UploadWorker()
{
    LOG_INFO("Started LocalFS UploadWorker");
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
        CheckBufferMemory();
        if (uploadContext != nullptr) {
            size_t bufferSize = uploadContext->m_fileData->BufferSize;
            UploadSync(move(uploadContext));
            m_uploadBufferMemory -= bufferSize;
        }
    }

    LOG_INFO("Shutdown LocalFS UploadWorker");
}
//---------------------------------------------------------------------------------------------------------------------
void LocalFSCacheClient::CheckBufferMemory()
{
    // Check memory usage at regular intervals (30 seconds) and log stats if usage is above warning threshold
    if (m_uploadBufferMemory > m_uploadBufferMemoryWarnThreshold)
    {
        {
            time_t currentTime = time(NULL);
            std::lock_guard lock(m_lockLogTime);
            if (difftime(currentTime, m_lastLoggedTime) >= 30)
            {
                LOG_WARN("LocalFSCacheClient high memory. Current buffer size in bytes={}", m_uploadBufferMemory);
                m_lastLoggedTime = currentTime;
            }
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
bool LocalFSCacheClient::UploadAsync(
    const std::string& mountName,
    const std::string& filePath,
    off64_t fileOffset,
    std::shared_ptr<const FileReadData> fileData)
{
    {
        CheckBufferMemory();

        const std::string cacheKey = GetCacheKey(filePath, fileOffset);
        lock_guard lock(m_mutexQueue);
        if ((m_uploadBufferMemory + fileData->BufferSize) > m_maxUploadBufferMemory)
        {
            LOG_INFO("Reached upload size limit, will skip upload. cacheKey={}, size={}, upload bytes pending={}",
                cacheKey, fileData->BufferSize, m_uploadBufferMemory);
            return false;
        }

        m_uploadBufferMemory += fileData->BufferSize;
        m_uploadQueue.push(make_unique<UploadContext>(cacheKey, fileData));
    }

    m_cvQueue.notify_one();
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
std::string LocalFSCacheClient::GetCacheKey(const std::string& filePath, off64_t fileOffset)
{
    return GetSHA256Hex(filePath + ":" + std::to_string(fileOffset));
}
//---------------------------------------------------------------------------------------------------------------------