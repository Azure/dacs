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
        request.lastaccessthreshold() +
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
bool DeleteFile(IConnection& clientConnection, const DeleteRequest& deleteRequest, enum ResponseCode& eResponseCode)
{
    LOG_INFO_REQID("filename: {}, group-id: {} lastaccessthreshold: {}",
        deleteRequest, deleteRequest.filename(), deleteRequest.groupid(), deleteRequest.lastaccessthreshold());

    DeleteResponse deleteResponse;
    const std::string& requestid = deleteRequest.requestid();
    if (!AuthenticateRequest(deleteRequest))
    {
        LOG_WARN_REQID("Failed to authenticate request", deleteRequest);
        SET_RESULT(deleteResponse, eResponseCode, DeleteResponse::AUTH_FAILED);
        return SendMessageToRemoteFinal(clientConnection, deleteResponse, requestid);
    }

    if (deleteRequest.type_case() == DeleteRequest::kGroupid)
    {
        if (deleteRequest.groupid().empty())
       {
            LOG_WARN_REQID("Delete failed: groupid cannot be empty", deleteRequest);
            SET_RESULT(deleteResponse, eResponseCode, DeleteResponse::INTERNAL_ERROR);
            return SendMessageToRemoteFinal(clientConnection, deleteResponse, requestid);
        }

        if (CacheManager::GetInstance().DeleteCacheEntryByGroupID(deleteRequest.groupid()))
        {
            LOG_INFO_REQID("Delete successful. groupId: {}", deleteRequest, deleteRequest.groupid());
            SET_RESULT(deleteResponse, eResponseCode, DeleteResponse::SUCCESS);
        }
        else
        {
            LOG_WARN_REQID("Failed to delete. groupId: {}", deleteRequest, deleteRequest.groupid());
            SET_RESULT(deleteResponse, eResponseCode, DeleteResponse::INTERNAL_ERROR);
        }
    }
    else if (deleteRequest.type_case() == DeleteRequest::kLastaccessthreshold)
    {
        if (deleteRequest.lastaccessthreshold().empty())
        {
            LOG_WARN_REQID("Delete failed: lastaccessthreshold cannot be empty", deleteRequest);
            SET_RESULT(deleteResponse, eResponseCode, DeleteResponse::INTERNAL_ERROR);
            return SendMessageToRemoteFinal(clientConnection, deleteResponse, requestid);
        }

        bool result =
            CacheManager::GetInstance().DeleteCacheEntryByLastAccessThreshold(deleteRequest.lastaccessthreshold());
        if (result)
        {
            LOG_INFO_REQID("Delete successful. lastaccessthreshold: {}", deleteRequest, deleteRequest.lastaccessthreshold());
            SET_RESULT(deleteResponse, eResponseCode, DeleteResponse::SUCCESS);
        }
        else
        {
            LOG_WARN_REQID("Failed to delete. lastaccessthreshold: {}", deleteRequest, deleteRequest.lastaccessthreshold());
            SET_RESULT(deleteResponse, eResponseCode, DeleteResponse::INTERNAL_ERROR);
        }
    }
    else
    {
        LOG_WARN_REQID("Invalid delete request", deleteRequest);
        SET_RESULT(deleteResponse, eResponseCode, DeleteResponse::INTERNAL_ERROR);
        return SendMessageToRemoteFinal(clientConnection, deleteResponse, requestid);
    }


    if (!SendMessageToRemoteFinal(clientConnection, deleteResponse, requestid))
    {
        LOG_WARN_REQID("Error sending delete response to client for groupId: {}", deleteRequest, deleteRequest.groupid());
        eResponseCode = ResponseCode::NETWORK_ERROR;
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------