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
//--------------------------------------------------------------------------------------------------------------------
inline ResponseCode TranslateResponseCode(UploadResponse_Result uploadResponseCode)
{
    ResponseCode eResponseCode = ResponseCode::NOT_SET;
    switch(uploadResponseCode)
    {
        case UploadResponse_Result::UploadResponse_Result_SUCCESS:
            eResponseCode = ResponseCode::SUCCESS;
            break;
        case UploadResponse_Result::UploadResponse_Result_FILE_EXISTS:
            eResponseCode = ResponseCode::INVALID_TRANSITION;
            break;
        case UploadResponse_Result::UploadResponse_Result_AUTH_FAILED:
        case UploadResponse_Result::UploadResponse_Result_BAD_REQUEST:
            eResponseCode = ResponseCode::BAD_REQUEST;
            break;
        case UploadResponse_Result::UploadResponse_Result_INTERNAL_ERROR:
            eResponseCode = ResponseCode::INTERNAL_ERROR;
            break;
        default:
            break;
    }

    return eResponseCode;
}
//---------------------------------------------------------------------------------------------------------------------
bool UploadFile(IConnection& clientConnection,
                const cache::UploadRequest& uploadRequest,
                enum ResponseCode& eResponseCode,
                std::size_t& nFileSize);
//---------------------------------------------------------------------------------------------------------------------