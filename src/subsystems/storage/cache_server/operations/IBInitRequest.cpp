///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "IBInitRequest.h"
#include "IBManager.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "Constants.h"
#include "Logger.h"
#include <string>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
bool AuthenticateRequest(const IBInitRequest& request)
{
    if (!IsAuthEnabled())
    {
        return true;
    }

    time_t timestamp = request.authparams().timestamp();
    string serializedHeaders = 
        std::string("IB") + 
        std::to_string(request.clientlid()) +
        std::to_string(request.clientibport()) +
        std::to_string(request.clientmtu()) +
        std::to_string(request.clientqpn_size()) +
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
bool IBInitializeClient(IConnection& clientConnection, const IBInitRequest& ibRequest)
{
    LOG_DEBUG("received ib init request={}", ibRequest.clientlid());

    IBInitResponse ibResponse;
    if (!AuthenticateRequest(ibRequest))
    {
        LOG_WARN("Failed to authenticate request");
        ibResponse.set_result(IBInitResponse::AUTH_FAILED);
        return SendMessageToRemoteFinal(clientConnection, ibResponse);
    }

    if (!IBManager::GetInstance().IBEnabled())
    {
        LOG_RARELY(5, WARN, "IB not initialized, client={}", ibRequest.clientlid());
        ibResponse.set_result(IBInitResponse::IB_NOT_SUPPORTED);
        return SendMessageToRemoteFinal(clientConnection, ibResponse);
    }

    uint32_t serverLid = 0;
    uint8_t serverIbPort = 0;
    uint8_t serverMtu = 0;

    bool bRet = IBManager::GetInstance().SetupQPConnections(
        ibRequest.clientlid(),
        ibRequest.clientibport(),
        ibRequest.clientmtu(),
        ibRequest.clientqpn(),
        serverLid,
        serverIbPort,
        serverMtu,
        *ibResponse.mutable_serverqpn());
    if (!bRet)
    {
        LOG_WARN("Failed to setup qp connections");
        ibResponse.set_result(IBInitResponse::INTERNAL_ERROR);
        return SendMessageToRemoteFinal(clientConnection, ibResponse);
    }

    LOG_DEBUG("ib init request succeeded={}", serverLid);
    ibResponse.set_serverlid(serverLid);
    ibResponse.set_serveribport(serverIbPort);
    ibResponse.set_servermtu(serverMtu);
    ibResponse.set_result(IBInitResponse::SUCCESS);
    return SendMessageToRemoteFinal(clientConnection, ibResponse);
}