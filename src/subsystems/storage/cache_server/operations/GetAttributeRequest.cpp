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
void PopulateAttributesInResponse(GetAttributeResponse& getAttributeResponse,
    const AttributesManager::CacheAttributes& cacheAttr)
{
    auto fileAttr = getAttributeResponse.mutable_fileattribute();
    fileAttr->set_isdir(cacheAttr.m_isdir);
    fileAttr->set_filesize(cacheAttr.m_filesize);
    fileAttr->set_accessedtime(cacheAttr.m_accessedtime);
    fileAttr->set_modifiedtime(cacheAttr.m_modifiedtime);
}
//---------------------------------------------------------------------------------------------------------------------
bool HandleGetAttribute(IConnection& clientConnection, const GetAttributeRequest& getAttributeRequest)
{
    LOG_DEBUG("filename received: {}, lock: {}", getAttributeRequest.filename(), getAttributeRequest.enablelock());

    GetAttributeResponse getAttributeResponse;
    if (!AuthenticateRequest(getAttributeRequest))
    {
        LOG_WARN("Failed to authenticate request");
        getAttributeResponse.set_result(GetAttributeResponse::AUTH_FAILED);
        return SendMessageToRemoteFinal(clientConnection, getAttributeResponse);
    }

    if (getAttributeRequest.enablelock())
    {
        AttributesManager::CacheAttributes cacheAttributes;
        bool bRet = AttributesManager::GetInstance().GetAndInitAttribute(
            getAttributeRequest.filename(), cacheAttributes);
        if (bRet == false)
        {
            LOG_DEBUG("Locked entry for get: {}", getAttributeRequest.filename())
            getAttributeResponse.set_result(GetAttributeResponse::NOT_FOUND_GOT_LOCK);
        }
        else if (cacheAttributes.m_filesize != -1)
        {
            LOG_DEBUG("Found entry in cache: {}, filesize: {}",
                getAttributeRequest.filename(), cacheAttributes.m_filesize);

            getAttributeResponse.set_result(GetAttributeResponse::SUCCESS);
            PopulateAttributesInResponse(getAttributeResponse, cacheAttributes);
        }
        else
        {
            LOG_DEBUG("Entry already locked by another client: {}", getAttributeRequest.filename())
            getAttributeResponse.set_result(GetAttributeResponse::NOT_FOUND);
        }
    }
    else
    {
        AttributesManager::CacheAttributes cacheAttributes;
        bool bRet = AttributesManager::GetInstance().GetAttribute(getAttributeRequest.filename(), cacheAttributes);
        if (bRet && cacheAttributes.m_filesize != -1)
        {
            LOG_DEBUG("Found entry in cache: {}, length: {}",
                getAttributeRequest.filename(), cacheAttributes.m_filesize);

            getAttributeResponse.set_result(GetAttributeResponse::SUCCESS);
            PopulateAttributesInResponse(getAttributeResponse, cacheAttributes);
        }
        else
        {
            LOG_DEBUG("Entry not found in cache: {}", getAttributeRequest.filename())
            getAttributeResponse.set_result(GetAttributeResponse::NOT_FOUND);
        }
    }

    if (!SendMessageToRemoteFinal(clientConnection, getAttributeResponse))
    {
        LOG_WARN("Error sending getAttributeRespone to client for entry: {}", getAttributeRequest.filename());
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------