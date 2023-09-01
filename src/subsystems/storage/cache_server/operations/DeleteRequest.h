///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include "cache.pb.h"
#include "IConnection.h"
#include "ResponseCode.h"

using namespace cache;
//----------------------------------------------------------------------------------------------------------------------
inline ResponseCode TranslateResponseCode(DeleteResponse_Result deleteResponseCode)
{
    ResponseCode eResponseCode = ResponseCode::NOT_SET;
    switch(deleteResponseCode)
    {
    case DeleteResponse_Result::DeleteResponse_Result_SUCCESS:
        eResponseCode = ResponseCode::SUCCESS;
        break;
    case DeleteResponse_Result::DeleteResponse_Result_AUTH_FAILED:
        eResponseCode = ResponseCode::BAD_REQUEST;
        break;
    case DeleteResponse_Result::DeleteResponse_Result_INTERNAL_ERROR:
        eResponseCode = ResponseCode::INTERNAL_ERROR;
        break;
    default:
        break;
    }
    return eResponseCode;
}
//---------------------------------------------------------------------------------------------------------------------
bool DeleteFile(IConnection& clientConnection, const cache::DeleteRequest& deleteRequest, enum ResponseCode& eResponseCode);
//---------------------------------------------------------------------------------------------------------------------