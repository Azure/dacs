///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "LockRequest.h"
#include "LockManager.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "Logger.h"
#include <iostream>
#include <string>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
bool AuthenticateRequest(const LockFileRequest& request)
{
    if (!IsAuthEnabled())
    {
        return true;
    }

    time_t timestamp = request.authparams().timestamp();
    string serializedHeaders = 
        std::string("L") + 
        request.filename() + 
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
bool LockFile(IConnection& clientConnection, const LockFileRequest& lockRequest)
{
    LOG_DEBUG_REQID("filename received: {}", lockRequest, lockRequest.filename());

    LockFileResponse lockResponse;
    if (!AuthenticateRequest(lockRequest))
    {
        LOG_WARN_REQID("Failed to authenticate request", lockRequest);
        lockResponse.set_result(LockFileResponse::AUTH_FAILED);
        return SendMessageToRemoteFinal(clientConnection, lockResponse);
    }

    if (LockManager::GetInstance().GetLockForUpload(lockRequest.filename()))
    {
        LOG_DEBUG_REQID("Locked file successfully: {}", lockRequest, lockRequest.filename());
        lockResponse.set_result(LockFileResponse::SUCCESS);
    }
    else
    {
        LOG_DEBUG_REQID("file already locked: {}", lockRequest, lockRequest.filename());
        lockResponse.set_result(LockFileResponse::ALREADY_LOCKED);
    }

    if (!SendMessageToRemoteFinal(clientConnection, lockResponse))
    {
        LOG_WARN_REQID("Error sending lock response to client for file: {}", lockRequest, lockRequest.filename());
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------