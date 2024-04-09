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
inline ResponseCode TranslateResponseCode(GetCacheLastAccessTimeDistributionResponse_Result responseCode)
{
    ResponseCode eResponseCode = ResponseCode::NOT_SET;
    switch(responseCode)
    {
    case GetCacheLastAccessTimeDistributionResponse_Result::GetCacheLastAccessTimeDistributionResponse_Result_SUCCESS:
        eResponseCode = ResponseCode::SUCCESS;
        break;
    case GetCacheLastAccessTimeDistributionResponse_Result::GetCacheLastAccessTimeDistributionResponse_Result_AUTH_FAILED:
        eResponseCode = ResponseCode::BAD_REQUEST;
        break;
    case GetCacheLastAccessTimeDistributionResponse_Result::GetCacheLastAccessTimeDistributionResponse_Result_INTERNAL_ERROR:
        eResponseCode = ResponseCode::INTERNAL_ERROR;
        break;
    default:
        break;
    }
    return eResponseCode;
}
//---------------------------------------------------------------------------------------------------------------------
bool GetLastAccessTimeDistribution(IConnection& clientConnection, const cache::GetCacheLastAccessTimeDistributionRequest& getCacheLastAccessTimeDistributionRequest, enum ResponseCode& eResponseCode);
//---------------------------------------------------------------------------------------------------------------------