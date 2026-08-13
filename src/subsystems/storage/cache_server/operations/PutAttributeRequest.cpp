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

#include "UploadRequest.h"
#include "AttributesManager.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "Constants.h"
#include "Logger.h"
#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fcntl.h>
#include <sstream>
#include <cstring>
#include <map>
#include <queue>
#include <chrono>
#include <set>
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
bool AuthenticateRequest(const PutAttributeRequest& request)
{
    if (!IsAuthEnabled())
    {
        return true;
    }

    time_t timestamp = request.authparams().timestamp();
    string serializedHeaders = 
        std::string("P") + 
        std::to_string(request.fileattributes_size()) + 
        std::to_string(timestamp);
    for (auto fileAttribute : request.fileattributes())
    {
        serializedHeaders += fileAttribute.filename();
        serializedHeaders += std::to_string(fileAttribute.fileattribute().filesize());
    }

   if (!request.authparams().enabled())
    {
        LOG_WARN_REQID("Auth params not sent by client", request);
        return false;
    }
    
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
bool HandlePutAttribute(IConnection& clientConnection, const PutAttributeRequest& putAttributeRequest)
{
    PutAttributeResponse putAttributeResponse;
    const std::string& requestid = putAttributeRequest.requestid();
    if (!AuthenticateRequest(putAttributeRequest))
    {
        LOG_WARN_REQID("Failed to authenticate request", putAttributeRequest);
        putAttributeResponse.set_result(PutAttributeResponse::AUTH_FAILED);
        return SendMessageToRemoteFinal(clientConnection, putAttributeResponse, requestid);
    }

    if (putAttributeRequest.fileattributes_size() > 0)
    {
        LOG_DEBUG_REQID("Num entries received: {}, first entry: {}",
            putAttributeRequest, putAttributeRequest.fileattributes_size(), putAttributeRequest.fileattributes(0).filename());
    }
    else
    {
        LOG_DEBUG_REQID("Num entries received: 0", putAttributeRequest);
    }

    bool bRet = AttributesManager::GetInstance().PutAttributes(putAttributeRequest);
    if (bRet)
    {
        LOG_DEBUG_REQID("Updated cache entries: {}", putAttributeRequest, putAttributeRequest.fileattributes_size());
        putAttributeResponse.set_result(PutAttributeResponse::SUCCESS);
    }
    else
    {
        LOG_WARN_REQID("Failed to update entries in cache", putAttributeRequest);
        putAttributeResponse.set_result(PutAttributeResponse::INTERNAL_ERROR);
    }

    if (!SendMessageToRemoteFinal(clientConnection, putAttributeResponse, requestid))
    {
        LOG_WARN_REQID("Error sending putAttributeResponse to client for entries: {}",
            putAttributeRequest, putAttributeRequest.fileattributes_size());
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------