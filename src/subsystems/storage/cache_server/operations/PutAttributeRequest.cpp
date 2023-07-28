///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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
    LOG_DEBUG_REQID("Num entries received: {}, first entry: {}",
        putAttributeRequest, putAttributeRequest.fileattributes_size(), putAttributeRequest.fileattributes(0).filename());

    PutAttributeResponse putAttributeResponse;
    if (!AuthenticateRequest(putAttributeRequest))
    {
        LOG_WARN_REQID("Failed to authenticate request", putAttributeRequest);
        putAttributeResponse.set_result(PutAttributeResponse::AUTH_FAILED);
        return SendMessageToRemoteFinal(clientConnection, putAttributeResponse);
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

    if (!SendMessageToRemoteFinal(clientConnection, putAttributeResponse))
    {
        LOG_WARN_REQID("Error sending putAttributeResponse to client for entries: {}",
            putAttributeRequest, putAttributeRequest.fileattributes_size());
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------