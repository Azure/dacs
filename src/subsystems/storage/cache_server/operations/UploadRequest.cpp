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

#include "UploadRequest.h"
#include "CacheManager.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "Constants.h"
#include "Logger.h"
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
#include <sys/types.h>
#include <sys/stat.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
// Forward declaration — buffered path defined below
static bool ReadFileDataWithBuffer(/*[in]*/ IConnection& clientConnection,
                        /*[in]*/ const cache::UploadRequest& uploadRequest,
                        /*[in]*/ const std::string& strTempFileName,
                        /*[out]*/ ResponseCode& eResponseCode,
                        /*[out]*/ cache::UploadResponse& uploadResponse,
                        /*[out]*/ uint64_t& totalRead);
//---------------------------------------------------------------------------------------------------------------------
/**
 * Zero-copy path: receive data from connection directly into a file
 * using splice() (socket -> pipe -> file) entirely in kernel space,
 * with fsync() to ensure data persistence.
 */
static bool ReadFileDataZeroCopy(/*[in]*/ IConnection& clientConnection,
                        /*[in]*/ const cache::UploadRequest& uploadRequest,
                        /*[in]*/ const std::string& strTempFileName,
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

    LOG_DEBUG_REQID("Reading file data (splice) for file: {}", uploadRequest, uploadRequest.filename());

    if (bNoDiscard)
    {
        int fileFd = open(strTempFileName.c_str(), O_WRONLY | O_TRUNC);
        if (fileFd < 0)
        {
            LOG_WARN_REQID("Failed to open temp file: {}, errno: {}:{}",
                           uploadRequest, strTempFileName, errno, strerror(errno));
            remove(strTempFileName.c_str());
            CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
            SET_RESULT(uploadResponse, eResponseCode, UploadResponse::INTERNAL_ERROR);
            SendMessageToRemoteFinal(clientConnection, uploadResponse, uploadRequest.requestid());
            return false;
        }

        bool bRecvOk = clientConnection.Recvfile(fileFd, uploadRequest.filesize());
        if (bRecvOk)
        {
            totalRead = uploadRequest.filesize();

            // Flush data to persistent storage before the caller renames the temp file.
            // A crash between close() and rename() only leaves a corrupted temp file,
            // so that is not a problem. However, a crash immediately after rename()
            // could potentially leave a partially-written file under the final cache path.
            if (fsync(fileFd) != 0)
            {
                LOG_WARN_REQID("fsync failed for temp file: {}, errno: {}:{}",
                               uploadRequest, strTempFileName, errno, strerror(errno));
                bRecvOk = false;
            }
        }

        if (close(fileFd) < 0)
        {
            LOG_WARN_REQID("Error closing temp file: {}, errno: {}:{}",
                           uploadRequest, strTempFileName, errno, strerror(errno));
            if (bRecvOk)
            {
                bRecvOk = false;
            }
        }

        if (!bRecvOk)
        {
            LOG_WARN_REQID("Recvfile failed for file: {}, expected: {} bytes",
                           uploadRequest, uploadRequest.filename(), uploadRequest.filesize());
            remove(strTempFileName.c_str());
            CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
            SET_RESULT(uploadResponse, eResponseCode, UploadResponse::INTERNAL_ERROR);
            SendMessageToRemoteFinal(clientConnection, uploadResponse, uploadRequest.requestid());
            return false;
        }
    }
    else
    {
        // Discard path: no file to splice into, fall back to buffered read.
        return ReadFileDataWithBuffer(clientConnection, uploadRequest, strTempFileName,
                                      eResponseCode, uploadResponse, totalRead);
    }

    STORAGE_ASSERT(totalRead == uploadRequest.filesize(),
                   "Read more bytes than expected. expected: {}. read: {}",
                   uploadRequest.filesize(),
                   totalRead);
    LOG_DEBUG_REQID("Successfully read file data (zero-copy) for file: {}", uploadRequest, uploadRequest.filename());

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
    // Set CACHESERVER_USE_ZEROCOPY=1 to enable splice/Recvfile zero-copy upload path.
    // Default is disabled (original buffered-write path).
    static const bool useZeroCopy = []() {
        const char* env = getenv("CACHESERVER_USE_ZEROCOPY");
        bool enabled = (env && std::string(env) == "1");
        LOG_INFO("ReadFileData: zero-copy path {}", enabled ? "ENABLED" : "DISABLED (default)");
        return enabled;
    }();

    if (useZeroCopy)
    {
        return ReadFileDataZeroCopy(clientConnection, uploadRequest, strTempFileName,
                                    eResponseCode, uploadResponse, totalRead);
    }

    return ReadFileDataWithBuffer(clientConnection, uploadRequest, strTempFileName,
                                   eResponseCode, uploadResponse, totalRead);
}
//---------------------------------------------------------------------------------------------------------------------
/**
 * Buffered read-file-data implementation using POSIX write() and fsync()
 * to ensure data persistence.
 * Extracted into its own function so that the buffer is only allocated
 * when this path is actually taken.
 */
static bool ReadFileDataWithBuffer(/*[in]*/ IConnection& clientConnection,
                        /*[in]*/ const cache::UploadRequest& uploadRequest,
                        /*[in]*/ const std::string& strTempFileName,
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

    const uint64_t writeBufferSize = 4 * 1024 * 1024;
    std::vector<char> buffer(writeBufferSize);
    int fileFd = -1;
    bool bWriteError = false;

    if (bNoDiscard)
    {
        fileFd = open(strTempFileName.c_str(), O_WRONLY | O_TRUNC);
        if (fileFd < 0)
        {
            LOG_WARN_REQID("Failed to open temp file: {}, errno: {}:{}",
                           uploadRequest, strTempFileName, errno, strerror(errno));
            remove(strTempFileName.c_str());
            CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
            SET_RESULT(uploadResponse, eResponseCode, UploadResponse::INTERNAL_ERROR);
            SendMessageToRemoteFinal(clientConnection, uploadResponse, uploadRequest.requestid());
            return false;
        }
    }

    while (totalRead < uploadRequest.filesize())
    {
        size_t bufferPos = 0;
        while (true)
        {
            const uint64_t remaining = min(writeBufferSize, uploadRequest.filesize() - totalRead)
                - static_cast<uint64_t>(bufferPos);
            int temp = clientConnection.Read(buffer.data() + bufferPos,
                                             static_cast<int>(remaining));
            if (temp <= 0)
            {
                LOG_WARN_REQID("read failed for file: {}. errno: {}:{}, Bytes expected: {}, read: {}",
                               uploadRequest,
                               uploadRequest.filename(),
                               errno,
                               strerror(errno),
                               uploadRequest.filesize(),
                               totalRead);

                if (fileFd >= 0)
                {
                    close(fileFd);
                    remove(strTempFileName.c_str());
                }

                CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
                SET_RESULT(uploadResponse, eResponseCode, UploadResponse::INTERNAL_ERROR);
                SendMessageToRemoteFinal(clientConnection, uploadResponse, uploadRequest.requestid());
                return false;
            }

            bufferPos += static_cast<size_t>(temp);

            if (bufferPos >= writeBufferSize ||
                totalRead + static_cast<uint64_t>(bufferPos) >= uploadRequest.filesize())
            {
                break;
            }
        }

        totalRead += static_cast<uint64_t>(bufferPos);

        // Write to disk using POSIX write(), handling short writes.
        // Data persistence is ensured by fsync() after the write loop completes.
        // On write failure, bWriteError is set so that subsequent iterations of
        // the outer while loop skip disk writes but continue reading from the
        // network connection, ensuring the client receives a proper error
        // response (not a broken pipe).
        if (bNoDiscard && !bWriteError)
        {
            size_t bytesWritten = 0;
            while (bytesWritten < bufferPos)
            {
                ssize_t w = ::write(fileFd, buffer.data() + bytesWritten, bufferPos - bytesWritten);
                if (w <= 0)
                {
                    if (w < 0 && errno == EINTR)
                    {
                        continue;
                    }
                    LOG_WARN_REQID("disk write failed for file: {}. errno: {}:{}, Bytes written to disk: {}/{}",
                                   uploadRequest,
                                   uploadRequest.filename(),
                                   errno,
                                   strerror(errno),
                                   totalRead - static_cast<uint64_t>(bufferPos) + bytesWritten,
                                   uploadRequest.filesize());
                    bWriteError = true;
                    break;
                }
                bytesWritten += static_cast<size_t>(w);
            }
        }
    }

    if (bNoDiscard)
    {
        // Flush data to persistent storage before the caller renames the temp file.
        // A crash between close() and rename() only leaves a corrupted temp file,
        // so that is not a problem. However, a crash immediately after rename()
        // could potentially leave a partially-written file under the final cache path.
        if (!bWriteError && fsync(fileFd) != 0)
        {
            LOG_WARN_REQID("fsync failed for file: {}. errno: {}:{}",
                           uploadRequest, uploadRequest.filename(), errno, strerror(errno));
            bWriteError = true;
        }

        if (close(fileFd) < 0)
        {
            LOG_WARN_REQID("Error closing temp file: {}, errno: {}:{}",
                           uploadRequest, strTempFileName, errno, strerror(errno));
            bWriteError = true;
        }

        if (bWriteError)
        {
            remove(strTempFileName.c_str());
            CacheManager::GetInstance().ResetCacheInfo(uploadRequest.filename(), uploadRequest.filesize());
            SET_RESULT(uploadResponse, eResponseCode, UploadResponse::INTERNAL_ERROR);
            SendMessageToRemoteFinal(clientConnection, uploadResponse, uploadRequest.requestid());
            return false;
        }
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
    uploadResponse.set_requestid(requestid);

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
