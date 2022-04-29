///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "UploadRequest.h"
#include "AttributesManager.h"
#include "Utils.h"
#include "SocketUtils.h"
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
        LOG_WARN("Auth params not sent by client");
        return false;
    }
    
    if (!ValidateAuthSignature(
        request.authparams().accountname(),
        request.authparams().signature(),
        serializedHeaders,
        timestamp))
    {
        LOG_WARN("Auth validation failure");
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool HandlePutAttribute(int clientFd, const PutAttributeRequest& putAttributeRequest)
{
    LOG_DEBUG("Num entries received: {}, first entry: {}",
        putAttributeRequest.fileattributes_size(), putAttributeRequest.fileattributes(0).filename());

    PutAttributeResponse putAttributeResponse;
    if (!AuthenticateRequest(putAttributeRequest))
    {
        LOG_WARN("Failed to authenticate request");
        putAttributeResponse.set_result(PutAttributeResponse::AUTH_FAILED);
        return SendMessageToRemote(clientFd, putAttributeResponse);
    }

    bool bRet = AttributesManager::GetInstance().PutAttributes(putAttributeRequest);
    if (bRet)
    {
        LOG_DEBUG("Updated cache entries: {}", putAttributeRequest.fileattributes_size());
        putAttributeResponse.set_result(PutAttributeResponse::SUCCESS);
    }
    else
    {
        LOG_WARN("Failed to update entries in cache");
        putAttributeResponse.set_result(PutAttributeResponse::INTERNAL_ERROR);
    }

    if (!SendMessageToRemote(clientFd, putAttributeResponse))
    {
        LOG_WARN("Error sending putAttributeResponse to client for entries: {}",
            putAttributeRequest.fileattributes_size());
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------