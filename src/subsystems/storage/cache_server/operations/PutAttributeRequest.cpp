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
bool HandlePutAttribute(int clientFd, const PutAttributeRequest& putAttributeRequest)
{
    LOG_DEBUG("Num entries received: {}, first entry: {}",
        putAttributeRequest.fileattributes_size(), putAttributeRequest.fileattributes(0).filename());

    PutAttributeResponse putAttributeResponse;
    bool bRet = PutAttributes(putAttributeRequest);
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