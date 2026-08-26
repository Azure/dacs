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

#include "GetCacheLastAccessTimeDistributionRequest.h"
#include "CacheManager.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "Logger.h"
#include <string>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
bool AuthenticateRequest(const GetCacheLastAccessTimeDistributionRequest& request)
{
    if (!IsAuthEnabled())
    {
        return true;
    }

    if (!request.authparams().enabled())
    {
        LOG_WARN_REQID("Auth params not sent by client", request);
        return false;
    }

    time_t timestamp = request.authparams().timestamp();
    string serializedHeaders = 
        std::string("GATD") + 
        std::to_string(request.distributionbucketsizeinhours()) +  
        std::to_string(timestamp);

    if (!ValidateAuthSignature(
        request.authparams().accountname(),
        request.authparams().signature(),
        serializedHeaders,
        timestamp))
    {
        LOG_WARN_REQID("Auth validation failure", request);
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool GetLastAccessTimeDistribution(
    IConnection& clientConnection,
    const GetCacheLastAccessTimeDistributionRequest& getLastAccessTimeDistributionRequest,
    enum ResponseCode& eResponseCode)
{
    LOG_INFO_REQID("Last access time Distribution Bucket {} h",
        getLastAccessTimeDistributionRequest, getLastAccessTimeDistributionRequest.distributionbucketsizeinhours());

    GetCacheLastAccessTimeDistributionResponse getCacheLastAccessTimeDistributionResponse;
    const std::string& requestid = getLastAccessTimeDistributionRequest.requestid();
    getCacheLastAccessTimeDistributionResponse.set_requestid(requestid);
    if (!AuthenticateRequest(getLastAccessTimeDistributionRequest))
    {
        LOG_WARN_REQID("Failed to authenticate request", getLastAccessTimeDistributionRequest);
        SET_RESULT(getCacheLastAccessTimeDistributionResponse, eResponseCode, GetCacheLastAccessTimeDistributionResponse::AUTH_FAILED);
        return SendMessageToRemoteFinal(clientConnection, getCacheLastAccessTimeDistributionResponse, requestid);
    }

    if (std::to_string(getLastAccessTimeDistributionRequest.distributionbucketsizeinhours()).empty() ||
        getLastAccessTimeDistributionRequest.distributionbucketsizeinhours() <= 0)
    {
        LOG_WARN_REQID("Request failed: Invalid distribution window ", getLastAccessTimeDistributionRequest);
        SET_RESULT(getCacheLastAccessTimeDistributionResponse, eResponseCode, GetCacheLastAccessTimeDistributionResponse::INTERNAL_ERROR);
        return SendMessageToRemoteFinal(clientConnection, getCacheLastAccessTimeDistributionResponse, requestid);
    }

    // TODO: the request field is in hours but CacheManager::GetLastAccessTimeDistribution
    // now takes seconds. Converting here as a temporary fix. Proper fix: make the wire protocol /
    // client pass the interval in seconds and rename distributionbucketsizeinhours accordingly.
    bool temp = CacheManager::GetInstance().GetLastAccessTimeDistribution(
        static_cast<uint64_t>(getLastAccessTimeDistributionRequest.distributionbucketsizeinhours()) * 3600ULL,
        *(getCacheLastAccessTimeDistributionResponse.mutable_distributionmap()));
    if (temp)
    {
        LOG_INFO_REQID("Get last access time distribution successful. distribution window: {} h", getLastAccessTimeDistributionRequest, getLastAccessTimeDistributionRequest.distributionbucketsizeinhours());
        SET_RESULT(getCacheLastAccessTimeDistributionResponse, eResponseCode, GetCacheLastAccessTimeDistributionResponse::SUCCESS);
    }
    else
    {
        LOG_WARN_REQID("Failed to get last access time distribution. distribution window: {} h", getLastAccessTimeDistributionRequest, getLastAccessTimeDistributionRequest.distributionbucketsizeinhours());
        SET_RESULT(getCacheLastAccessTimeDistributionResponse, eResponseCode, GetCacheLastAccessTimeDistributionResponse::INTERNAL_ERROR);
    }

    if (!SendMessageToRemoteFinal(clientConnection, getCacheLastAccessTimeDistributionResponse, requestid))
    {
        LOG_WARN_REQID("Error sending getLastAccessTimeDistribution response to client for distribution window : {} h", getLastAccessTimeDistributionRequest, getLastAccessTimeDistributionRequest.distributionbucketsizeinhours());
        eResponseCode = ResponseCode::NETWORK_ERROR;
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------