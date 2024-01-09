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
/**
 * Read client connection to a temp file, or discard it if temp filename is empty.
 * In case of errors, send final response to client and close connection.
 * 
 * @param clientConnection client connection to read from
 * @param uploadRequest upload request message
 * @param strTempFileName file name to write to temporarily, if empty, don't write to disc and discard the data.
 * @param eResponseCode response code, will be set only in case of error
 * @param uploadResponse upload response message, will be set only in case of error
 * @param totalRead gets updated with total bytes read from client connection
 * 
 * @return true if read was successful, false in case of errors
*/
bool ReadFileData(/*[in]*/ IConnection& clientConnection,
                  /*[in]*/ const cache::UploadRequest& uploadRequest,
                  /*[inopt]*/ const std::string& strTempFileName,
                  /*[out]*/ ResponseCode& eResponseCode,
                  /*[out]*/ cache::UploadResponse& uploadResponse,
                  /*[out]*/ uint64_t& totalRead)
{
    const bool bNoDiscard = (!strTempFileName.empty());

    if (uploadRequest.filesize() == 0)
    {
        LOG_INFO_REQID("No data to read for file: '{}'", uploadRequest, uploadRequest.filename());
        return true;
    }

    LOG_DEBUG_REQID("Reading file data for file: {}", uploadRequest, uploadRequest.filename());

    // TODO: Verify if sufficient disk space is available for this write and reserve it

    const uint64_t writeBufferSize = 4 * 1024 * 1024;
    char buffer[writeBufferSize];
    std::ofstream fileOutput;

    if (bNoDiscard)
    {
        fileOutput = std::ofstream(strTempFileName, ofstream::out | ofstream::binary | ofstream::trunc);
    }

    // TODO: we can try sendfile() for uploads too
    while (totalRead < uploadRequest.filesize())
    {
        int bufferPos = 0;
        while (true)
        {
            int temp = clientConnection.Read(buffer + bufferPos,
                                             min(writeBufferSize, uploadRequest.filesize() - totalRead) - bufferPos);
            if (temp <= 0)
            {
                LOG_WARN_REQID("read failed for file: {}. errno: {}:{}, Bytes expected: {}, read: {}",
                               uploadRequest,
                               uploadRequest.filename(),
                               errno,
                               strerror(errno),
                               uploadRequest.filesize(),
                               totalRead);

                if (bNoDiscard)
                {
                    fileOutput.close();
                    remove(strTempFileName.c_str());
                }

                CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
                SET_RESULT(uploadResponse, eResponseCode, UploadResponse::INTERNAL_ERROR);
                SendMessageToRemoteFinal(clientConnection, uploadResponse, uploadRequest.requestid());
                return false;
            }

            bufferPos += temp;

            if (bufferPos >= writeBufferSize || totalRead + bufferPos >= uploadRequest.filesize())
            {
                break;
            }
        }

        totalRead += bufferPos;

        if (bNoDiscard)
        {
            fileOutput.write(buffer, bufferPos);
        }
    }

    if (bNoDiscard)
    {
        fileOutput.flush();
        fileOutput.close();
    }

    STORAGE_ASSERT(totalRead == uploadRequest.filesize(),
                   "Read more bytes than expected. expected: {}. read: {}",
                   uploadRequest.filesize(),
                   totalRead);
    LOG_DEBUG_REQID("Successfully read file data for file: {}", uploadRequest, uploadRequest.filename());

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
/* Params & return code same as `ReadData` with `strTempFileName` passed empty */
inline bool ReadAndDiscardFileData(IConnection& clientConnection,
                                   const UploadRequest& uploadRequest,
                                   ResponseCode& eResponseCode,
                                   UploadResponse& uploadResponse)
{
    std::string strEmptyFileName = "";
    uint64_t _nTotalRead = 0;

    LOG_INFO_REQID("Reading and discarding file data for file: {}, limit {} bytes",
                   uploadRequest,
                   uploadRequest.filename(),
                   MaxReadDiscardSizeBytes);

    if (uploadRequest.filesize() > MaxReadDiscardSizeBytes)
    {
        LOG_WARN_REQID("file: '{}' size {} greater than limit {} for invalid uploads. Closing connection.",
                       uploadRequest,
                       uploadRequest.filename(),
                       uploadRequest.filesize(),
                       MaxReadDiscardSizeBytes);
        SET_RESULT(uploadResponse, eResponseCode, UploadResponse::INTERNAL_ERROR);
        SendMessageToRemoteFinal(clientConnection, uploadResponse, uploadRequest.requestid());
        return false;
    }

    return ReadFileData(clientConnection, uploadRequest, strEmptyFileName, eResponseCode, uploadResponse, _nTotalRead);
}
//---------------------------------------------------------------------------------------------------------------------
bool UploadFile(IConnection& clientConnection,
                const cache::UploadRequest& uploadRequest,
                ResponseCode& eResponseCode,
                std::size_t& nFileSize)
{
    LOG_DEBUG_REQID("filename received: {}", uploadRequest, uploadRequest.filename());

    UploadResponse uploadResponse;
    const std::string& requestid = uploadRequest.requestid();
    if (!AuthenticateRequest(uploadRequest))
    {
        LOG_WARN_REQID("Failed to authenticate request", uploadRequest);
        if (ReadAndDiscardFileData(
               clientConnection, uploadRequest, eResponseCode, uploadResponse))
        {
            SET_RESULT(uploadResponse, eResponseCode, UploadResponse::AUTH_FAILED);
            return SendMessageToRemoteFinal(clientConnection, uploadResponse, requestid);
        }
        return false;
    }

    // Note: On the behavior of connection reuse
    // The return value of this function indicates whether the server should teardown or reuse the client connection
    // However, the client sends two messages in succession - first the upload request params, then the actual file contents
    // before even parsing the server's response - this means the file's contents are in flight before the server can properly
    // parse the request message.
    // This has a few implications:
    // 1. If server encounters an error while parsing the request message, read the entire request payload before return, lest the in-flight
    // data be misinterpreted as a new request 
    // 2. Once the request has been parsed, and validated - the server begins to read the actual in-flight data of the file.
    // After all the data has been read, barring any genuine transmission/receive errors, encountered errors do not require the connection
    // to be torn down

    // Set limit on filename length
    if (uploadRequest.filename().size() > UPLOAD_FILENAME_LIMIT)
    {
        LOG_WARN_REQID("filename too large, size: {}", uploadRequest, uploadRequest.filename().size());
        if (ReadAndDiscardFileData(
               clientConnection, uploadRequest, eResponseCode, uploadResponse))
        {
            SET_RESULT(uploadResponse, eResponseCode, UploadResponse::BAD_REQUEST);
            return SendMessageToRemoteFinal(clientConnection, uploadResponse, requestid);
        }
        return false;
    }

    CacheManager::CacheInfo cacheInfo = CacheManager::GetInstance().GetCacheInfo(uploadRequest.filename());

    // Duplicate file upload
    if (cacheInfo.m_status == CacheManager::CacheStatus::COMPLETED)
    {
        LOG_WARN_REQID("Duplicate upload - file '{}' already exists in cache", uploadRequest, uploadRequest.filename());
        if (!ReadAndDiscardFileData(
               clientConnection, uploadRequest, eResponseCode, uploadResponse))
        {
            return false;
        }

        SET_RESULT(uploadResponse, eResponseCode, UploadResponse::FILE_EXISTS);
        return SendMessageToRemoteFinal(clientConnection, uploadResponse, requestid);
    }

    // Note: we let the client try to upload if another client has also started for a few reasons:
    // 1. Redundancy - if at the last moment the other client fails to upload, 
    // we still have a chance to succeed uploading the file
    // 2. This case is usually rare - lease expiry scenarios, or if the client is not taking the lock to upload
    if (cacheInfo.m_status == CacheManager::CacheStatus::STARTED)
    {
        LOG_WARN_REQID("file already being uploaded by another client {}, will attempt to upload anyway",
            uploadRequest, uploadRequest.filename());
    }

    if (uploadRequest.filesize() > UPLOAD_FILESIZE_LIMIT)
    {
        LOG_WARN_REQID("filesize too large: {}", uploadRequest, uploadRequest.filesize());
        SET_RESULT(uploadResponse, eResponseCode, UploadResponse::BAD_REQUEST);
        SendMessageToRemoteFinal(clientConnection, uploadResponse, requestid);
        return false;
    }

    if (!CacheManager::GetInstance().InitCacheInfo(uploadRequest.filename(), uploadRequest.ignorelock(),
        uploadRequest.filesize()))
    {
        LOG_WARN_REQID("error starting upload for: {}", uploadRequest, uploadRequest.filename());
        SET_RESULT(uploadResponse, eResponseCode, UploadResponse::INTERNAL_ERROR);
        SendMessageToRemoteFinal(clientConnection, uploadResponse, requestid);
        return false;
    }

    LOG_DEBUG_REQID("file size to upload: {}", uploadRequest, uploadRequest.filesize());
    if (uploadRequest.filesize() > 0)
    {
        // write to temp file on disk
        string tempFilename = GetCacheTempPath(uploadRequest.filename());
        if (tempFilename.empty())
        {
            LOG_WARN_REQID("Couldn't create a temp file for uploading: {}", uploadRequest, uploadRequest.filename());
            CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
            SET_RESULT(uploadResponse, eResponseCode, UploadResponse::INTERNAL_ERROR);
            SendMessageToRemoteFinal(clientConnection, uploadResponse, requestid);
            return false;
        }

        LOG_DEBUG_REQID("Using temp file {} for uploading {}", uploadRequest, tempFilename, uploadRequest.filename());

        uint64_t totalRead = 0;

        // Read upload data to temp file
        if (!ReadFileData(clientConnection, uploadRequest, tempFilename, eResponseCode, uploadResponse, totalRead))
        {
            return false;
        }

        if (totalRead != uploadRequest.filesize())
        {
            LOG_WARN_REQID("Read more bytes than expected. expected: {}. read: {}",
                uploadRequest,
                uploadRequest.filesize(),
                totalRead);
            remove(tempFilename.c_str());
            CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
            SET_RESULT(uploadResponse, eResponseCode, UploadResponse::INTERNAL_ERROR);
            SendMessageToRemoteFinal(clientConnection, uploadResponse, requestid);
            return false;
        }

        // Required data has been correctly read at this point, connection can be safely reused 
        // Now atomically rename file to final target
        string destFilename = GetCachePath(uploadRequest.filename());
        int ret = rename(tempFilename.c_str(), destFilename.c_str());
        if (ret != 0)
        {
            LOG_WARN_REQID("Failed to rename file from {} to {}. errno: {}:{}",
                uploadRequest, tempFilename, destFilename, errno, strerror(errno));
            remove(tempFilename.c_str());
            SET_RESULT(uploadResponse, eResponseCode, UploadResponse::INTERNAL_ERROR);
            CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
        }
        else
        {
            if (!CacheManager::GetInstance().UpdateCacheInfo(uploadRequest.filename(), uploadRequest.filesize(),
                uploadRequest.metadata(), uploadRequest.expiryseconds(), uploadRequest.groupid()))
            {
                // UpdateCacheInfo returns false only if the file is already cached
                SET_RESULT(uploadResponse, eResponseCode, UploadResponse::FILE_EXISTS);
                LOG_WARN_REQID("Cache entry upload status already marked as COMPLETED: {} - duplicate upload scenario", uploadRequest, uploadRequest.filename());
            }
            else
            {
                SET_RESULT(uploadResponse, eResponseCode, UploadResponse::SUCCESS);
                nFileSize = uploadRequest.filesize();
                LOG_DEBUG_REQID("File successfully written to cache: {}", uploadRequest, uploadRequest.filename());
            }
        }
    }
    else
    {
        SET_RESULT(uploadResponse, eResponseCode, UploadResponse::SUCCESS);
        if (!CacheManager::GetInstance().UpdateCacheInfo(uploadRequest.filename(), uploadRequest.filesize(),
            uploadRequest.metadata(), uploadRequest.expiryseconds(), uploadRequest.groupid()))
        {
            // UpdateCacheInfo returns false only if the file is already cached
            SET_RESULT(uploadResponse, eResponseCode, UploadResponse::FILE_EXISTS);
            LOG_WARN_REQID("Cache entry upload status already marked as COMPLETED: {} - duplicate upload scenario", uploadRequest, uploadRequest.filename());
        }
        else
        {
            LOG_DEBUG_REQID("File successfully written to cache: {}", uploadRequest, uploadRequest.filename());
            nFileSize = uploadRequest.filesize();
        }
    }

    if (!SendMessageToRemoteFinal(clientConnection, uploadResponse, requestid))
    {
        LOG_WARN_REQID("Error sending response to client for file: {}", uploadRequest, uploadRequest.filename());
        eResponseCode = ResponseCode::NETWORK_ERROR;
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------