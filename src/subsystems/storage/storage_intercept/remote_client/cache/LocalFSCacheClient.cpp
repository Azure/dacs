///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "LocalFSCacheClient.h"
#include "StorageInterceptLogger.h"
#include "StorageDlsymWrapper.h"
#include "StorageInterceptConfig.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
LocalFSCacheClient::LocalFSCacheClient()
: m_strLocalCachePath(StorageInterceptConfig::GetInstance().GetCacheLocalFSCachePath())
{
    if (::mkdir(m_strLocalCachePath.c_str(), 0777) != 0 && errno != EEXIST)
    {
        LOG_CRITICAL("Cannot create local directory: {}", m_strLocalCachePath);
        throw ("Cannot create local directory: " + m_strLocalCachePath);
    }

    LOG_INFO("Configured LocalFSCacheClient. local cache path: {}", m_strLocalCachePath);
}
//---------------------------------------------------------------------------------------------------------------------
LocalFSCacheClient::~LocalFSCacheClient()
{
}
//---------------------------------------------------------------------------------------------------------------------
ICacheClient::CacheStatus LocalFSCacheClient::DownloadSync(
    const std::string& mountName,
    const std::string& cacheKey,
    uint64_t startOffset,
    uint64_t length,
    char *buffer,
    uint64_t& readLength)
{
    ICacheClient::CacheStatus cacheStatus = ICacheClient::INTERNAL_ERROR;
    string filePath = m_strLocalCachePath + "/" + cacheKey;

    int fileFD = StorageDlsymWrapper::open(filePath.c_str(), O_RDONLY, 0777);
    if (fileFD >= 0)
    {
        uint64_t fileLength = StorageDlsymWrapper::lseek64(fileFD, 0, SEEK_END);
        if (fileLength < 0)
        {
            LOG_WARN("Invalid fileLength, file: {}", filePath);
            StorageDlsymWrapper::close(fileFD);
            return cacheStatus;
        }

        if (StorageDlsymWrapper::lseek64(fileFD, 0, SEEK_SET) < 0)
        {
            LOG_WARN("Cannot seek to file start, file: {}", filePath);
            StorageDlsymWrapper::close(fileFD);
            return cacheStatus;
        }

        uint64_t fileOffset = StorageDlsymWrapper::lseek64(fileFD, startOffset, SEEK_SET);
        if (fileOffset != startOffset)
        {
            LOG_WARN("Invalid offset: {}, file: {}", startOffset, filePath);
            StorageDlsymWrapper::close(fileFD);
            return cacheStatus;
        }

        readLength = std::min(fileLength - startOffset, length);

        uint64_t totalRead = 0;
        while (totalRead < readLength)
        {
            int tempRead;
            if ((tempRead = StorageDlsymWrapper::read(fileFD, buffer + totalRead, readLength - totalRead)) > 0)
            {
                totalRead += tempRead;
            }
            else 
            {
                LOG_WARN("Failed to read() message from file: {}", filePath);
                StorageDlsymWrapper::close(fileFD);
                return cacheStatus;
            }
        }
        if (totalRead != readLength)
        {
            LOG_WARN("Failed to read correct length from server. got: {}, expected: {}, file: {}",
                totalRead, readLength, filePath);
            StorageDlsymWrapper::close(fileFD);
            return cacheStatus;
        }
        StorageDlsymWrapper::close(fileFD);
        cacheStatus = ICacheClient::SUCCESS;
    }
    else
    {
        cacheStatus = ICacheClient::NOTFOUND_LOCKED;
    }
    return cacheStatus;
}
//---------------------------------------------------------------------------------------------------------------------
bool LocalFSCacheClient::UploadAsync(
    const std::string& mountName,
    const std::string& cacheKey,
    std::shared_ptr<const FileReadData> fileData)
{
    string filePath = m_strLocalCachePath + "/" + cacheKey;
    int fileFD = StorageDlsymWrapper::open(filePath.c_str(), O_WRONLY | O_CREAT, 0644);
    if (fileFD < 0)
    {
        LOG_WARN("Cannot open file for writing: {}", filePath);
        return false;        
    }

    uint64_t totalWritten = 0;
    while (totalWritten < fileData->BufferSize)
    {
        int bufferLen = fileData->BufferSize - totalWritten;
        int tempWrite;
        if ((tempWrite = write(
            fileFD, 
            static_cast<const char*>(fileData->Buffer.get()) + totalWritten,
            bufferLen)) > 0)
        {
            totalWritten += tempWrite;
        }
        else 
        {
            LOG_WARN("couldn't complete write to file: {}. wrote: {}, actual length: {}",
                filePath, totalWritten, fileData->BufferSize);
            StorageDlsymWrapper::close(fileFD);
            return false;
        }
    }

    StorageDlsymWrapper::close(fileFD);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
