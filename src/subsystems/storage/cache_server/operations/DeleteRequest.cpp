///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "DeleteRequest.h"
#include "CacheManager.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "Logger.h"
#include <string>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
bool AuthenticateRequest(const DeleteRequest& request)
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
        std::string("Del") + 
        request.filename() + 
        request.groupid() + 
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
bool DeleteFile(IConnection& clientConnection, const DeleteRequest& deleteRequest)
{
    LOG_INFO_REQID("filename: {}, group-id: {}",
        deleteRequest, deleteRequest.filename(), deleteRequest.groupid());

    DeleteResponse deleteResponse;
    if (!AuthenticateRequest(deleteRequest))
    {
        LOG_WARN_REQID("Failed to authenticate request", deleteRequest);
        deleteResponse.set_result(DeleteResponse::AUTH_FAILED);
        return SendMessageToRemoteFinal(clientConnection, deleteResponse);
    }

    if (deleteRequest.type_case() != DeleteRequest::kGroupid || deleteRequest.groupid().empty())
    {
        LOG_WARN_REQID("Delete failed: groupid cannot be empty", deleteRequest);
        deleteResponse.set_result(DeleteResponse::INTERNAL_ERROR);
        return SendMessageToRemoteFinal(clientConnection, deleteResponse);
    }

    if (CacheManager::GetInstance().DeleteCacheEntryByGroupID(deleteRequest.groupid()))
    {
        LOG_INFO_REQID("Delete successful. groupId: {}", deleteRequest, deleteRequest.groupid());
        deleteResponse.set_result(DeleteResponse::SUCCESS);
    }
    else
    {
        LOG_WARN_REQID("Failed to delete. groupId: {}", deleteRequest, deleteRequest.groupid());
        deleteResponse.set_result(DeleteResponse::INTERNAL_ERROR);
    }

    if (!SendMessageToRemoteFinal(clientConnection, deleteResponse))
    {
        LOG_WARN_REQID("Error sending delete response to client for groupId: {}", deleteRequest, deleteRequest.groupid());
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------