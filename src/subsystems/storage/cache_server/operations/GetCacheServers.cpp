///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "GetCacheServers.h"
#include "CacheManager.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "Logger.h"
#include <string>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
bool GetCacheServers(IConnection& clientConnection,
                     const GetCacheServersRequest& getCacheServersRequest,
                     enum ResponseCode& eResponseCode)
{
    const std::string& requestid = getCacheServersRequest.requestid();
    LOG_DEBUG_REQID_STR("Get Cache Servers", requestid);

    GetCacheServersResponse getCacheServersResponse;
    
    if (!GetCacheServersHelper(*getCacheServersResponse.mutable_serveraddresses(), requestid))
    {
        SET_RESULT(getCacheServersResponse, eResponseCode, GetCacheServersResponse::INTERNAL_ERROR);
        return SendMessageToRemoteFinal(clientConnection, getCacheServersResponse, requestid);
    }

    SET_RESULT(getCacheServersResponse, eResponseCode, GetCacheServersResponse::SUCCESS);
    return SendMessageToRemoteFinal(clientConnection, getCacheServersResponse, requestid);
}