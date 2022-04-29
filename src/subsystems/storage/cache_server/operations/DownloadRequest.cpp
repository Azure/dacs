///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "DownloadRequest.h"
#include "CacheManager.h"
#include "Utils.h"
#include "SocketUtils.h"
#include "Constants.h"
#include "Logger.h"
#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fcntl.h>
#include <sstream>
#include <cstring>
#include <signal.h>
#include <sys/stat.h>
#include <map>
#include <queue>
#include <chrono>
#include <set>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
bool AuthenticateRequest(const DownloadRequest& request)
{
    if (!IsAuthEnabled())
    {
        return true;
    }

    time_t timestamp = request.authparams().timestamp();
    string serializedHeaders = 
        std::string("D") + 
        request.filename() + 
        std::to_string(request.offset()) +
        std::to_string(request.length()) +
        (request.enablelock() ? "1" : "0") +
        std::to_string(timestamp);
    if (!request.authparams().enabled())
    {
        LOG_WARN("Auth params not sent by client");
        return false;
    }

    if (!ValidateAuthSignature(
        request.authparams().accountname(),
        request.authparams().signature(),
        serializedHeaders,
        timestamp))
    {
        LOG_WARN("Auth validation failure");
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool DownloadFile(int clientFd, const DownloadRequest& downloadRequest)
{
    LOG_DEBUG("filename: {}, offset: {}, length: {}",
        downloadRequest.filename(), downloadRequest.offset(), downloadRequest.length());

    DownloadResponse downloadResponse;
    if (!AuthenticateRequest(downloadRequest))
    {
        LOG_WARN("Failed to authenticate request");
        downloadResponse.set_result(DownloadResponse::AUTH_FAILED);
        return SendMessageToRemote(clientFd, downloadResponse);
    }

    // Set limit on filename length
    if (downloadRequest.filename().size() > UPLOAD_FILENAME_LIMIT)
    {
        LOG_WARN("filename too large, size: {}", downloadRequest.filename().size());
        downloadResponse.set_result(DownloadResponse::FILENAME_LIMIT_EXCEEDED);
        return SendMessageToRemote(clientFd, downloadResponse);
    }

    CacheManager::CacheInfo cacheInfo;
    if (downloadRequest.enablelock())
    {
        cacheInfo = CacheManager::GetInstance().GetAndInitCacheInfo(downloadRequest.filename());
        if (cacheInfo.m_status == CacheManager::NOTFOUND)
        {
            // TODO: Handle scenario when this response doesn't make it to the client.
            //       Subsequent clients will never get a lock and the file will not be cached.
            LOG_DEBUG("Locked file for download: {}", downloadRequest.filename())
            downloadResponse.set_result(DownloadResponse::NOT_FOUND_GOT_LOCK);
        }
        else if (cacheInfo.m_status == CacheManager::COMPLETED)
        {
            LOG_DEBUG("Found file in cache: {}, length: {}", downloadRequest.filename(), cacheInfo.m_fileSize);
            downloadResponse.set_result(DownloadResponse::SUCCESS);
        }
        else
        {
            LOG_DEBUG("File already locked by another client: {}", downloadRequest.filename())
            downloadResponse.set_result(DownloadResponse::NOT_FOUND_ALREADY_LOCKED);
        }
    }
    else
    {
        cacheInfo = CacheManager::GetInstance().GetCacheInfo(downloadRequest.filename());
        if (cacheInfo.m_status == CacheManager::COMPLETED)
        {
            LOG_DEBUG("Found file in cache: {}, length: {}", downloadRequest.filename(), cacheInfo.m_fileSize);
            downloadResponse.set_result(DownloadResponse::SUCCESS);
        }
        else
        {
            LOG_DEBUG("File not found in cache: {}", downloadRequest.filename())
            downloadResponse.set_result(DownloadResponse::NOT_FOUND);
        }
    }

    // update download filesize and metadata
    uint64_t downloadFileSize = 0;
    if (cacheInfo.m_status == CacheManager::COMPLETED)
    {
        if (downloadRequest.offset() > cacheInfo.m_fileSize)
        {
            LOG_WARN("Download failure, file: {}, offset: {} more than cached filesize: {}",
                downloadRequest.filename(), downloadRequest.offset(), cacheInfo.m_fileSize);
            downloadResponse.set_result(DownloadResponse::INVALID_OFFSET_LENGTH);
            return SendMessageToRemote(clientFd, downloadResponse);
        }
        
        if (downloadRequest.length() == 0)
        {
            downloadFileSize = cacheInfo.m_fileSize - downloadRequest.offset();
        }
        else
        {
            downloadFileSize = std::min(cacheInfo.m_fileSize - downloadRequest.offset(), downloadRequest.length());
        }
        LOG_DEBUG("Set download filesize to: {}", downloadFileSize);

        *(downloadResponse.mutable_metadata()) = cacheInfo.m_metadata;

    }

    downloadResponse.set_filesize(downloadFileSize);
    if (cacheInfo.m_status == CacheManager::COMPLETED && downloadFileSize > 0) 
    {
        int corkState = 1;
        if (setsockopt(clientFd, IPPROTO_TCP, TCP_CORK, &corkState, sizeof(corkState)) != 0)
        {
            LOG_WARN("Error setting TCP_CORK, will ignore error and continue, errno: {}:{}",
                errno, strerror(errno));
        }
        if (!SendMessageToRemote(clientFd, downloadResponse))
        {
            LOG_WARN("Error sending response to client for file: {}", downloadRequest.filename());
            return false;
        }

        string destFilename = GetCachePath(downloadRequest.filename());
        int fileFD = open(destFilename.c_str(), O_RDONLY, 0);
        if (fileFD < 0)
        {
            LOG_WARN("Error opening local file: {}, filename: {}, errno: {}:{}",
                destFilename, downloadRequest.filename(), errno, strerror(errno));
            if (errno == ENOENT)
            {
                // Local file not present, cache state is not consistent with filesystem, reset the cache entry
                LOG_WARN("Resetting cache for file: {}", downloadRequest.filename());
                CacheManager::GetInstance().ResetCacheInfo(downloadRequest.filename(), downloadFileSize);
            }
            return false;
        }

        // loop to serve entire file
        uint64_t totalWritten = 0;
        while (totalWritten < downloadFileSize)
        {
            off_t startOffset = downloadRequest.offset() + totalWritten;
            ssize_t written = sendfile(clientFd, fileFD, &startOffset, downloadFileSize - totalWritten);
            if (written <= 0)
            {
                LOG_WARN("Error during sendfile: {}, errno: {}:{}. total bytes written: {}",
                    destFilename, errno, strerror(errno), totalWritten);
                close(fileFD);
                return false;
            }
            if (corkState == 1)
            {
                corkState = 0;
                if (setsockopt(clientFd, IPPROTO_TCP, TCP_CORK, &corkState, sizeof(corkState)) != 0)
                {
                    LOG_WARN("Error re-setting TCP_CORK, will ignore error and continue, errno: {}:{}",
                        errno, strerror(errno));
                }
            }

            totalWritten += written;
        }
        close(fileFD);
        LOG_INFO("downloaded and sent file to client successfully: {}", downloadRequest.filename());
    }
    else
    {
        if (!SendMessageToRemote(clientFd, downloadResponse))
        {
            LOG_WARN("Error sending response to client for file: {}", downloadRequest.filename());
            return false;
        }
        return true;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------