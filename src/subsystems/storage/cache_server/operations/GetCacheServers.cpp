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
    getCacheServersResponse.set_requestid(requestid);
    
    if (!GetCacheServersHelper(*getCacheServersResponse.mutable_serveraddresses(), requestid))
    {
        SET_RESULT(getCacheServersResponse, eResponseCode, GetCacheServersResponse::INTERNAL_ERROR);
        return SendMessageToRemoteFinal(clientConnection, getCacheServersResponse, requestid);
    }

    SET_RESULT(getCacheServersResponse, eResponseCode, GetCacheServersResponse::SUCCESS);
    return SendMessageToRemoteFinal(clientConnection, getCacheServersResponse, requestid);
}