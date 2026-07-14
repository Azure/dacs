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
//-----------------------------------------------------------------------------------------------------------------------
inline ResponseCode TranslateResponseCode(IBInitResponse_Result ibInitResponseCode)
{
    ResponseCode eResponseCode = ResponseCode::NOT_SET;
    switch(ibInitResponseCode)
    {
        case IBInitResponse_Result::IBInitResponse_Result_SUCCESS:
            eResponseCode = ResponseCode::SUCCESS;
            break;
        case IBInitResponse_Result::IBInitResponse_Result_AUTH_FAILED:
        case IBInitResponse_Result::IBInitResponse_Result_IB_NOT_SUPPORTED:
            eResponseCode = ResponseCode::BAD_REQUEST;
            break;
        case IBInitResponse_Result::IBInitResponse_Result_INTERNAL_ERROR:
            eResponseCode = ResponseCode::INTERNAL_ERROR;
            break;
        default:
            break;
    }

    return eResponseCode;
}
//---------------------------------------------------------------------------------------------------------------------
bool IBInitializeClient(IConnection& clientConnection, const cache::IBInitRequest& ibInitRequest, enum ResponseCode& eResponseCode);
//---------------------------------------------------------------------------------------------------------------------