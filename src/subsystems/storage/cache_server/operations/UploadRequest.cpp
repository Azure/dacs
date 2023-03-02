///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "UploadRequest.h"
#include "CacheManager.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "Constants.h"
#include "Logger.h"
#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fcntl.h>
#include <sstream>
#include <cstring>
#include <map>
#include <queue>
#include <chrono>
#include <set>
#include <memory>
#include <atomic>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
bool AuthenticateRequest(const UploadRequest& request)
{
    if (!IsAuthEnabled())
    {
        return true;
    }

    time_t timestamp = request.authparams().timestamp();
    string serializedHeaders = 
        std::string("U") + 
        request.filename() + 
        std::to_string(request.filesize()) +
        (request.ignorelock() ? "1" : "0") +
        std::to_string(request.expiryseconds()) +
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
bool UploadFile(IConnection& clientConnection, const UploadRequest& uploadRequest)
{
    LOG_DEBUG("filename received: {}", uploadRequest.filename());

    UploadResponse uploadResponse;
    if (!AuthenticateRequest(uploadRequest))
    {
        LOG_WARN("Failed to authenticate request");
        uploadResponse.set_result(UploadResponse::AUTH_FAILED);
        SendMessageToRemoteFinal(clientConnection, uploadResponse);
        return false;
    }

    // Set limit on filename length
    if (uploadRequest.filename().size() > UPLOAD_FILENAME_LIMIT)
    {
        LOG_WARN("filename too large, size: {}", uploadRequest.filename().size());
        uploadResponse.set_result(UploadResponse::BAD_REQUEST);
        SendMessageToRemoteFinal(clientConnection, uploadResponse);
        return false;
    }

    CacheManager::CacheInfo cacheInfo = CacheManager::GetInstance().GetCacheInfo(uploadRequest.filename());
    if (cacheInfo.m_status == CacheManager::CacheStatus::COMPLETED)
    {
        LOG_WARN("file already uploaded {}", uploadRequest.filename());
        uploadResponse.set_result(UploadResponse::FILE_EXISTS);
        SendMessageToRemoteFinal(clientConnection, uploadResponse);
        return false;
    }

    if (cacheInfo.m_status == CacheManager::CacheStatus::STARTED)
    {
        LOG_WARN("file already being uploaded by another client {}, will attempt to upload anyway",
            uploadRequest.filename());
    }

    if (uploadRequest.filesize() > UPLOAD_FILESIZE_LIMIT)
    {
        LOG_WARN("filesize too large: {}", uploadRequest.filesize());
        uploadResponse.set_result(UploadResponse::BAD_REQUEST);
        SendMessageToRemoteFinal(clientConnection, uploadResponse);
        return false;
    }

    if (!CacheManager::GetInstance().InitCacheInfo(uploadRequest.filename(), uploadRequest.ignorelock(),
        uploadRequest.filesize()))
    {
        LOG_WARN("error starting upload for: {}", uploadRequest.filename());
        uploadResponse.set_result(UploadResponse::INTERNAL_ERROR);
        SendMessageToRemoteFinal(clientConnection, uploadResponse);
        return false;
    }

    LOG_DEBUG("file size to upload: {}", uploadRequest.filesize());
    if (uploadRequest.filesize() > 0)
    {
        // write to temp file on disk
        string tempFilename = GetCacheTempPath(uploadRequest.filename());
        if (tempFilename.empty())
        {
            LOG_WARN("Couldn't create a temp file for uploading: {}", uploadRequest.filename());
            CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
            uploadResponse.set_result(UploadResponse::INTERNAL_ERROR);
            SendMessageToRemoteFinal(clientConnection, uploadResponse);
            return false;
        }

        LOG_DEBUG("Using temp file {} for uploading {}", tempFilename, uploadRequest.filename());
        std::ofstream fileOutput(tempFilename, ofstream::out | ofstream::binary | ofstream::trunc);

        // TODO: Verify if sufficient disk space is available for this write and reserve it

        const int writeBufferSize = 4 * 1024 * 1024;
        char buffer[writeBufferSize];
        uint64_t totalRead = 0;

        // TODO: we can try sendfile() for uploads too 
        while (totalRead < uploadRequest.filesize())
        {
            int bufferPos = 0;
            while (true)
            {
                int temp = clientConnection.Read(buffer + bufferPos, writeBufferSize - bufferPos);
                if (temp <= 0)
                {
                    LOG_WARN("read failed for file: {}. errno: {}:{}, Bytes expected: {}, read: {}",
                        uploadRequest.filename(),
                        errno,
                        strerror(errno),
                        uploadRequest.filesize(),
                        totalRead);
                    fileOutput.close();
                    remove(tempFilename.c_str());
                    CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
                    uploadResponse.set_result(UploadResponse::INTERNAL_ERROR);
                    SendMessageToRemoteFinal(clientConnection, uploadResponse);
                    return false;
                }
                bufferPos += temp;
                if (bufferPos >= writeBufferSize || totalRead + bufferPos >= uploadRequest.filesize())
                {
                    break;
                }
            }
            totalRead += bufferPos;
            fileOutput.write(buffer, bufferPos);
        }

        fileOutput.flush();
        fileOutput.close();

        if (totalRead != uploadRequest.filesize())
        {
            LOG_WARN("Read more bytes than expected. expected: {}. read: {}",
                uploadRequest.filesize(),
                totalRead);
            remove(tempFilename.c_str());
            CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
            uploadResponse.set_result(UploadResponse::INTERNAL_ERROR);
            SendMessageToRemoteFinal(clientConnection, uploadResponse);
            return false;
        }

        // Now atomically rename file to final target
        string destFilename = GetCachePath(uploadRequest.filename());
        int ret = rename(tempFilename.c_str(), destFilename.c_str());
        if (ret != 0)
        {
            LOG_WARN("Failed to rename file from {} to {}. errno: {}:{}",
                tempFilename, destFilename, errno, strerror(errno));
            remove(tempFilename.c_str());
            uploadResponse.set_result(UploadResponse::INTERNAL_ERROR);
            CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
        }
        else
        {
            if (!CacheManager::GetInstance().UpdateCacheInfo(uploadRequest.filename(), uploadRequest.filesize(),
                uploadRequest.metadata(), uploadRequest.expiryseconds(), uploadRequest.groupid()))
            {
                uploadResponse.set_result(UploadResponse::INTERNAL_ERROR);
                LOG_ERROR("error updating upload status to COMPLETED: {}", uploadRequest.filename());
            }
            else
            {
                uploadResponse.set_result(UploadResponse::SUCCESS);
                LOG_DEBUG("File successfully written to cache: {}", uploadRequest.filename());
            }
        }
    }
    else
    {
        uploadResponse.set_result(UploadResponse::SUCCESS);
        if (!CacheManager::GetInstance().UpdateCacheInfo(uploadRequest.filename(), uploadRequest.filesize(),
            uploadRequest.metadata(), uploadRequest.expiryseconds(), uploadRequest.groupid()))
        {
            uploadResponse.set_result(UploadResponse::INTERNAL_ERROR);
            LOG_WARN("error updating upload status to COMPLETED: {}", uploadRequest.filename());
        }
        else
        {
            LOG_DEBUG("File successfully written to cache: {}", uploadRequest.filename());
        }
    }

    if (!SendMessageToRemoteFinal(clientConnection, uploadResponse))
    {
        LOG_WARN("Error sending response to client for file: {}", uploadRequest.filename());
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------