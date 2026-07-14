///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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