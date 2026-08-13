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

#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include "cache.pb.h"
#include "IConnection.h"
#include "ResponseCode.h"

using namespace cache;

#define SET_DOWNLOAD_RESULT(responseObject, responseVariable, response) \
do { \
    responseObject.set_result(response); \
    responseVariable = TranslateResponseCode(response); \
} while (0)
//----------------------------------------------------------------------------------------------------------------------
inline ResponseCode TranslateResponseCode(DownloadResponse_Result downloadResponseCode)
{
    ResponseCode eResponseCode = ResponseCode::NOT_SET;
    switch(downloadResponseCode)
    {
        case DownloadResponse_Result::DownloadResponse_Result_SUCCESS:
            eResponseCode = ResponseCode::SUCCESS;
            break;
        case DownloadResponse_Result::DownloadResponse_Result_NOT_FOUND_GOT_LOCK:
        case DownloadResponse_Result::DownloadResponse_Result_NOT_FOUND_ALREADY_LOCKED:
        case DownloadResponse_Result::DownloadResponse_Result_NOT_FOUND:
            eResponseCode = ResponseCode::INVALID_TRANSITION;
            break;
        case DownloadResponse_Result::DownloadResponse_Result_INVALID_OFFSET_LENGTH:
        case DownloadResponse_Result::DownloadResponse_Result_FILENAME_LIMIT_EXCEEDED:
        case DownloadResponse_Result::DownloadResponse_Result_AUTH_FAILED:
            eResponseCode = ResponseCode::BAD_REQUEST;
            break;
        case DownloadResponse_Result::DownloadResponse_Result_INTERNAL_ERROR:
            eResponseCode = ResponseCode::INTERNAL_ERROR;
            break;
        default:
            break;
    }
    return eResponseCode;
}
//---------------------------------------------------------------------------------------------------------------------
bool DownloadFile(IConnection& clientConnection,
                  const cache::DownloadRequest& downloadRequest,
                  enum ResponseCode& eResponseCode,
                  std::size_t& nFileSize);
//---------------------------------------------------------------------------------------------------------------------