///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "LockRequest.h"
#include "Utils.h"
#include "SocketUtils.h"
#include "Logger.h"
#include <iostream>
#include <string>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
bool LockFile(int clientFd, const LockFileRequest& lockRequest)
{
    LOG_DEBUG("filename received: {}", lockRequest.filename());

    LockFileResponse lockResponse;
    if (GetLockForUpload(lockRequest.filename()))
    {
        LOG_INFO("Locked file successfully: {}", lockRequest.filename());
        lockResponse.set_result(LockFileResponse::SUCCESS);
    }
    else
    {
        LOG_INFO("file already locked: {}", lockRequest.filename());
        lockResponse.set_result(LockFileResponse::ALREADY_LOCKED);
    }

    if (!SendMessageToRemote(clientFd, lockResponse))
    {
        LOG_WARN("Error sending lock response to client for file: {}", lockRequest.filename());
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------