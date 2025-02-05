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
inline ResponseCode TranslateResponseCode(GetCacheServersResponse_Result getCacheServersResponseCode)
{
    ResponseCode eResponseCode = ResponseCode::NOT_SET;
    switch (getCacheServersResponseCode)
    {
    case GetCacheServersResponse_Result::GetCacheServersResponse_Result_SUCCESS:
        eResponseCode = ResponseCode::SUCCESS;
        break;
    case GetCacheServersResponse_Result::GetCacheServersResponse_Result_INTERNAL_ERROR:
        eResponseCode = ResponseCode::INTERNAL_ERROR;
        break;
    default:
        break;
    }
    return eResponseCode;
}
//---------------------------------------------------------------------------------------------------------------------
bool GetCacheServers(IConnection& clientConnection,
                     const cache::GetCacheServersRequest& getCacheServersRequest,
                     enum ResponseCode& eResponseCode);
//---------------------------------------------------------------------------------------------------------------------