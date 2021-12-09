///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "DownloadRequest.h"
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
bool DownloadFile(int clientFd, const DownloadRequest& downloadRequest)
{
    LOG_DEBUG("filename: {}, offset: {}, length: {}",
        downloadRequest.filename(), downloadRequest.offset(), downloadRequest.length());

    DownloadResponse downloadResponse;

    // Set limit on filename length
    if (downloadRequest.filename().size() > UPLOAD_FILENAME_LIMIT)
    {
        LOG_WARN("filename too large, size: {}", downloadRequest.filename().size());
        downloadResponse.set_result(DownloadResponse::FILENAME_LIMIT_EXCEEDED);
        return SendMessageToRemote(clientFd, downloadResponse);
    }

    CacheInfo cacheInfo;
    if (downloadRequest.enablelock())
    {
        cacheInfo = GetAndInitCacheStatus(downloadRequest.filename());
        if (cacheInfo.m_status == NOTFOUND)
        {
            // TODO: Handle scenario when this response doesn't make it to the client.
            //       Subsequent clients will never get a lock and the file will not be cached.
            LOG_DEBUG("Locked file for download: {}", downloadRequest.filename())
            downloadResponse.set_result(DownloadResponse::NOT_FOUND_GOT_LOCK);
        }
        else if (cacheInfo.m_status == COMPLETED)
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
        cacheInfo = GetCacheStatus(downloadRequest.filename());
        if (cacheInfo.m_status == COMPLETED)
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

    // update download filesize
    uint64_t downloadFileSize = 0;
    if (cacheInfo.m_status == COMPLETED)
    {
        if (downloadRequest.offset() > cacheInfo.m_fileSize)
        {
            LOG_WARN("offset more than cached filesize: {}, cached filesize: {}",
                downloadRequest.offset(), cacheInfo.m_fileSize);
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
    }

    downloadResponse.set_filesize(downloadFileSize);
    if (cacheInfo.m_status == COMPLETED && downloadFileSize > 0) 
    {
        int corkState = 1;
        if (setsockopt(clientFd, IPPROTO_TCP, TCP_CORK, &corkState, sizeof(corkState)) != 0)
        {
            LOG_WARN("Error setting TCP_CORK, will ignore error and continue");
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
            LOG_WARN("Error opening cached file: {}", destFilename.c_str());
            return false;
        }

        // loop to serve entire file
        uint64_t totalWritten = 0;
        while (totalWritten < downloadFileSize)
        {
            off_t startOffset = downloadRequest.offset() + totalWritten;
            uint64_t written = sendfile(clientFd, fileFD, &startOffset, downloadFileSize - totalWritten);
            if (written <= 0)
            {
                LOG_WARN("Error during sendfile: {}. total bytes wrote: {}", destFilename.c_str(), totalWritten);
                close(fileFD);
                return false;
            }
            if (corkState == 1)
            {
                corkState = 0;
                if (setsockopt(clientFd, IPPROTO_TCP, TCP_CORK, &corkState, sizeof(corkState)) != 0)
                {
                    LOG_WARN("Error re-setting TCP_CORK, will ignore error and continue");
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