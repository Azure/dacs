/// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "DownloadRequest.h"
#include "CacheManager.h"
#include "Utils.h"
#include "ConnectionUtils.h"
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
        LOG_WARN_REQID("Auth params not sent by client", request);
        return false;
    }

    if (!ValidateAuthSignature(
        request.authparams().accountname(),
        request.authparams().signature(),
        serializedHeaders,
        timestamp))
    {
        LOG_WARN_REQID("Auth validation failure", request);
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool DownloadFile(IConnection& clientConnection, const DownloadRequest& downloadRequest, enum ResponseCode& eResponseCode, std::size_t& nFileSize)
{
    LOG_DEBUG_REQID("filename: {}, offset: {}, length: {}",
        downloadRequest, downloadRequest.filename(), downloadRequest.offset(), downloadRequest.length());

    DownloadResponse downloadResponse;
    const std::string& requestid = downloadRequest.requestid();
    if (!AuthenticateRequest(downloadRequest))
    {
        LOG_WARN_REQID("Failed to authenticate request", downloadRequest);
        SET_RESULT(downloadResponse, eResponseCode, DownloadResponse::AUTH_FAILED);
        return SendMessageToRemoteFinal(clientConnection, downloadResponse, requestid);
    }

    // Set limit on filename length
    if (downloadRequest.filename().size() > UPLOAD_FILENAME_LIMIT)
    {
        LOG_WARN_REQID("filename too large, size: {}", downloadRequest, downloadRequest.filename().size());
        SET_RESULT(downloadResponse, eResponseCode, DownloadResponse::FILENAME_LIMIT_EXCEEDED);
        return SendMessageToRemoteFinal(clientConnection, downloadResponse, requestid);
    }

    CacheManager::CacheInfo cacheInfo(CacheManager::CacheStatus::NOTFOUND);
    if (downloadRequest.enablelock())
    {
        cacheInfo = CacheManager::GetInstance().GetAndInitCacheInfo(downloadRequest.filename());
        if (cacheInfo.m_status == CacheManager::CacheStatus::NOTFOUND)
        {
            // TODO: Handle scenario when this response doesn't make it to the client.
            //       Subsequent clients will never get a lock and the file will not be cached.
            LOG_DEBUG_REQID("Locked file for download: {}", downloadRequest, downloadRequest.filename())
            SET_RESULT(downloadResponse, eResponseCode, DownloadResponse::NOT_FOUND_GOT_LOCK);
        }
        else if (cacheInfo.m_status == CacheManager::CacheStatus::COMPLETED)
        {
            LOG_DEBUG_REQID("Found file in cache: {}, length: {}", downloadRequest, downloadRequest.filename(), cacheInfo.m_fileSize);
            SET_RESULT(downloadResponse, eResponseCode, DownloadResponse::SUCCESS);
        }
        else
        {
            LOG_DEBUG_REQID("File already locked by another client: {}", downloadRequest, downloadRequest.filename())
            SET_RESULT(downloadResponse, eResponseCode, DownloadResponse::NOT_FOUND_ALREADY_LOCKED);
        }
    }
    else
    {
        cacheInfo = CacheManager::GetInstance().GetCacheInfo(downloadRequest.filename());
        if (cacheInfo.m_status == CacheManager::CacheStatus::COMPLETED)
        {
            LOG_DEBUG_REQID("Found file in cache: {}, length: {}", downloadRequest, downloadRequest.filename(), cacheInfo.m_fileSize);
            SET_RESULT(downloadResponse, eResponseCode, DownloadResponse::SUCCESS);
        }
        else
        {
            LOG_DEBUG_REQID("File not found in cache: {}", downloadRequest, downloadRequest.filename())
            SET_RESULT(downloadResponse, eResponseCode, DownloadResponse::NOT_FOUND);
        }
    }

    // update download filesize and metadata
    uint64_t downloadFileSize = 0;

    if (cacheInfo.m_status == CacheManager::CacheStatus::COMPLETED)
    {
        if (downloadRequest.offset() > cacheInfo.m_fileSize)
        {
            LOG_WARN_REQID("Download failure, file: {}, offset: {} more than cached filesize: {}",
                downloadRequest, downloadRequest.filename(), downloadRequest.offset(), cacheInfo.m_fileSize);
            SET_RESULT(downloadResponse, eResponseCode, DownloadResponse::INVALID_OFFSET_LENGTH);
            return SendMessageToRemoteFinal(clientConnection, downloadResponse, requestid);
        }
        
        if (downloadRequest.length() == 0)
        {
            downloadFileSize = cacheInfo.m_fileSize - downloadRequest.offset();
        }
        else
        {
            downloadFileSize = std::min(cacheInfo.m_fileSize - downloadRequest.offset(), downloadRequest.length());
        }

        LOG_DEBUG_REQID("Set download filesize to: {}", downloadRequest, downloadFileSize);
        *(downloadResponse.mutable_metadata()) = cacheInfo.m_metadata;
    }

    downloadResponse.set_filesize(downloadFileSize);
    
    downloadResponse.set_isfulldownload(cacheInfo.m_fileSize == downloadFileSize);

    if (cacheInfo.m_status == CacheManager::CacheStatus::COMPLETED && downloadFileSize > 0) 
    {
        if (!SendMessageToRemote(clientConnection, downloadResponse, requestid))
        {
            LOG_WARN_REQID("Error sending response to client for file: {}", downloadRequest, downloadRequest.filename());
            eResponseCode = ResponseCode::NETWORK_ERROR;
            return false;
        }

        string destFilename = GetCachePath(downloadRequest.filename());
        int fileFD = open(destFilename.c_str(), O_RDONLY, 0);
        if (fileFD < 0)
        {
            LOG_WARN_REQID("Error opening local file: {}, filename: {}, errno: {}:{}",
                downloadRequest, destFilename, downloadRequest.filename(), errno, strerror(errno));
            if (errno == ENOENT)
            {
                // Local file not present, cache state is not consistent with filesystem, reset the cache entry
                LOG_WARN_REQID("Resetting cache for file: {}", downloadRequest, downloadRequest.filename());
                CacheManager::GetInstance().ResetCacheInfo(downloadRequest.filename(), downloadFileSize);
            }
            eResponseCode = ResponseCode::INTERNAL_ERROR;
            return false;
        }

        bool bRet = clientConnection.Sendfile(fileFD, downloadRequest.offset(), downloadFileSize);
        if (close(fileFD) < 0)
        {
            LOG_WARN_REQID("Error closing local file: {}, filename: {}, errno: {}:{}",
                downloadRequest, destFilename, downloadRequest.filename(), errno, strerror(errno));
        }
        if (!bRet)
        {
            LOG_WARN_REQID("Failed to send file: {} to client id: {}", downloadRequest, downloadRequest.filename(), clientConnection.GetId());
            eResponseCode = ResponseCode::NETWORK_ERROR;
            return false;
        }
        nFileSize = downloadFileSize;
        LOG_DEBUG_REQID("downloaded and sent file to client successfully: {}", downloadRequest, downloadRequest.filename());
    }
    else
    {
        if (!SendMessageToRemoteFinal(clientConnection, downloadResponse, requestid))
        {
            LOG_WARN_REQID("Error sending response to client for file: {}", downloadRequest, downloadRequest.filename());
            eResponseCode = ResponseCode::NETWORK_ERROR;
            return false;
        }
        nFileSize = downloadFileSize;
        return true;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------