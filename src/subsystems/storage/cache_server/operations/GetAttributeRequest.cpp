///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
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
bool AuthenticateRequest(const GetAttributeRequest& request)
{
    if (!IsAuthEnabled())
    {
        return true;
    }

    time_t timestamp = request.authparams().timestamp();
    string serializedHeaders = 
        std::string("G") + 
        request.filename() + 
        (request.enablelock() ? "1" : "0") +
        std::to_string(timestamp);

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
void PopulateAttributesInResponse(GetAttributeResponse& getAttributeResponse,
    const AttributesManager::CacheAttributes& cacheAttr)
{
    auto fileAttr = getAttributeResponse.mutable_fileattribute();
    fileAttr->set_isdir(cacheAttr.m_isdir);
    fileAttr->set_filesize(cacheAttr.m_filesize);
    fileAttr->set_accessedtime(cacheAttr.m_accessedtime);
    fileAttr->set_modifiedtime(cacheAttr.m_modifiedtime);
    fileAttr->set_version(cacheAttr.m_version);
}
//---------------------------------------------------------------------------------------------------------------------
bool HandleGetAttribute(IConnection& clientConnection, const GetAttributeRequest& getAttributeRequest)
{
    LOG_DEBUG_REQID("filename received: {}, lock: {}", getAttributeRequest, getAttributeRequest.filename(), getAttributeRequest.enablelock());

    GetAttributeResponse getAttributeResponse;
    const std::string& requestid = getAttributeRequest.requestid();
    if (!AuthenticateRequest(getAttributeRequest))
    {
        LOG_WARN_REQID("Failed to authenticate request", getAttributeRequest);
        getAttributeResponse.set_result(GetAttributeResponse::AUTH_FAILED);
        return SendMessageToRemoteFinal(clientConnection, getAttributeResponse, requestid);
    }

    if (getAttributeRequest.enablelock())
    {
        AttributesManager::CacheAttributes cacheAttributes;
        bool bRet = AttributesManager::GetInstance().GetAndInitAttribute(
            getAttributeRequest.filename(), cacheAttributes);
        if (bRet == false)
        {
            LOG_DEBUG_REQID("Locked entry for get: {}", getAttributeRequest, getAttributeRequest.filename());
            getAttributeResponse.set_result(GetAttributeResponse::NOT_FOUND_GOT_LOCK);
        }
        else if (cacheAttributes.m_filesize != -1)
        {
            LOG_DEBUG_REQID("Found entry in cache: {}, filesize: {}",
                getAttributeRequest, getAttributeRequest.filename(), cacheAttributes.m_filesize);

            getAttributeResponse.set_result(GetAttributeResponse::SUCCESS);
            PopulateAttributesInResponse(getAttributeResponse, cacheAttributes);
        }
        else
        {
            LOG_DEBUG_REQID("Entry already locked by another client: {}", getAttributeRequest, getAttributeRequest.filename());
            getAttributeResponse.set_result(GetAttributeResponse::NOT_FOUND);
        }
    }
    else
    {
        AttributesManager::CacheAttributes cacheAttributes;
        bool bRet = AttributesManager::GetInstance().GetAttribute(getAttributeRequest.filename(), cacheAttributes);
        if (bRet && cacheAttributes.m_filesize != -1)
        {
            LOG_DEBUG_REQID("Found entry in cache: {}, length: {}",
                getAttributeRequest, getAttributeRequest.filename(), cacheAttributes.m_filesize);

            getAttributeResponse.set_result(GetAttributeResponse::SUCCESS);
            PopulateAttributesInResponse(getAttributeResponse, cacheAttributes);
        }
        else
        {
            LOG_DEBUG_REQID("Entry not found in cache: {}", getAttributeRequest, getAttributeRequest.filename());
            getAttributeResponse.set_result(GetAttributeResponse::NOT_FOUND);
        }
    }

    if (!SendMessageToRemoteFinal(clientConnection, getAttributeResponse, requestid))
    {
        LOG_WARN_REQID("Error sending getAttributeRespone to client for entry: {}", getAttributeRequest, getAttributeRequest.filename());
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------