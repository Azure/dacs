///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "UploadRequest.h"
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
bool HandleGetAttribute(int clientFd, const GetAttributeRequest& getAttributeRequest)
{
    LOG_DEBUG("filename received: {}, lock: {}", getAttributeRequest.filename(), getAttributeRequest.enablelock());

    GetAttributeResponse getAttributeResponse;
    if (getAttributeRequest.enablelock())
    {
        CacheAttributes cacheAttributes;
        bool bRet = GetAndInitAttribute(getAttributeRequest.filename(), cacheAttributes);
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
            auto fileAttr = getAttributeResponse.mutable_fileattribute();
            fileAttr->set_isdir(cacheAttributes.m_isdir);
            fileAttr->set_filesize(cacheAttributes.m_filesize);
            fileAttr->set_accessedtime(cacheAttributes.m_accessedtime);
            fileAttr->set_modifiedtime(cacheAttributes.m_modifiedtime);
        }
        else
        {
            LOG_DEBUG("Entry already locked by another client: {}", getAttributeRequest.filename())
            getAttributeResponse.set_result(GetAttributeResponse::NOT_FOUND);
        }
    }
    else
    {
        CacheAttributes cacheAttributes;
        bool bRet = GetAttribute(getAttributeRequest.filename(), cacheAttributes);
        if (bRet && cacheAttributes.m_filesize != -1)
        {
            LOG_DEBUG("Found entry in cache: {}, length: {}",
                getAttributeRequest.filename(), cacheAttributes.m_filesize);

            getAttributeResponse.set_result(GetAttributeResponse::SUCCESS);
            auto fileAttr = getAttributeResponse.mutable_fileattribute();
            fileAttr->set_isdir(cacheAttributes.m_isdir);
            fileAttr->set_filesize(cacheAttributes.m_filesize);
            fileAttr->set_accessedtime(cacheAttributes.m_accessedtime);
            fileAttr->set_modifiedtime(cacheAttributes.m_modifiedtime);
        }
        else
        {
            LOG_DEBUG("Entry not found in cache: {}", getAttributeRequest.filename())
            getAttributeResponse.set_result(GetAttributeResponse::NOT_FOUND);
        }
    }

    if (!SendMessageToRemote(clientFd, getAttributeResponse))
    {
        LOG_WARN("Error sending getAttributeRespone to client for entry: {}", getAttributeRequest.filename());
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------