/// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
        request.lastaccesstime() +
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
    LOG_INFO_REQID("filename: {}, group-id: {} lastaccesstime: {}",
        deleteRequest, deleteRequest.filename(), deleteRequest.groupid(), deleteRequest.lastaccesstime());

    DeleteResponse deleteResponse;
    const std::string& requestid = deleteRequest.requestid();
    deleteResponse.set_requestid(requestid);
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
    else if (deleteRequest.type_case() == DeleteRequest::kLastaccesstime)
    {
        if (deleteRequest.lastaccesstime().empty())
        {
            LOG_WARN_REQID("Delete failed: lastaccesstime cannot be empty", deleteRequest);
            SET_RESULT(deleteResponse, eResponseCode, DeleteResponse::INTERNAL_ERROR);
            return SendMessageToRemoteFinal(clientConnection, deleteResponse, requestid);
        }

        bool result =
            CacheManager::GetInstance().DeleteCacheEntryByLastAccessTime(deleteRequest.lastaccesstime());
        if (result)
        {
            LOG_INFO_REQID("Delete successful. lastaccesstime: {}", deleteRequest, deleteRequest.lastaccesstime());
            SET_RESULT(deleteResponse, eResponseCode, DeleteResponse::SUCCESS);
        }
        else
        {
            LOG_WARN_REQID("Failed to delete. lastaccesstime: {}", deleteRequest, deleteRequest.lastaccesstime());
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