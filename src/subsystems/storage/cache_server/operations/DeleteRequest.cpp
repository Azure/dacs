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
        LOG_WARN("Auth params not sent by client");
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
        LOG_WARN("Auth validation failure");
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool DeleteFile(IConnection& clientConnection, const DeleteRequest& deleteRequest)
{
    LOG_DEBUG("filename: {}, group-id: {}",
        deleteRequest.filename(), deleteRequest.groupid());

    DeleteResponse deleteResponse;
    if (!AuthenticateRequest(deleteRequest))
    {
        LOG_WARN("Failed to authenticate request");
        deleteResponse.set_result(DeleteResponse::AUTH_FAILED);
        return SendMessageToRemoteFinal(clientConnection, deleteResponse);
    }

    if (deleteRequest.type_case() != DeleteRequest::kGroupid || deleteRequest.groupid().empty())
    {
        LOG_WARN("Delete failed: groupid cannot be empty");
        deleteResponse.set_result(DeleteResponse::INTERNAL_ERROR);
        return SendMessageToRemoteFinal(clientConnection, deleteResponse);
    }

    if (CacheManager::GetInstance().DeleteCacheEntryByGroupID(deleteRequest.groupid()))
    {
        LOG_INFO("Delete successful. groupId: {}", deleteRequest.groupid());
        deleteResponse.set_result(DeleteResponse::SUCCESS);
    }
    else
    {
        LOG_WARN("Failed to delete. groupId: {}", deleteRequest.groupid());
        deleteResponse.set_result(DeleteResponse::INTERNAL_ERROR);
    }

    if (!SendMessageToRemoteFinal(clientConnection, deleteResponse))
    {
        LOG_WARN("Error sending delete response to client for groupId: {}", deleteRequest.groupid());
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------