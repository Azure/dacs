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

#include "IBInitRequest.h"
#include "IBManager.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "Constants.h"
#include "Logger.h"
#include <string>
#include <vector>
#include <limits>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
bool IBInitializeClient(IConnection& clientConnection, const IBInitRequest& ibRequest, enum ResponseCode& eResponseCode)
{
    LOG_INFO_REQID("received ib init request={}", ibRequest, ibRequest.clientlid());

    IBInitResponse ibResponse;
    const std::string& requestid = ibRequest.requestid();
    ibResponse.set_requestid(requestid);

    if (!IBManager::GetInstance().IBEnabled())
    {
        LOG_RARELY(5, WARN, "IB not initialized, client={} request ID: {}", ibRequest.clientlid(), ibRequest.requestid());
        SET_RESULT(ibResponse, eResponseCode, IBInitResponse::IB_NOT_SUPPORTED);
        return SendMessageToRemoteFinal(clientConnection, ibResponse, requestid);
    }

    uint32_t serverLid = 0;
    uint8_t serverIbPort = 0;
    uint8_t serverMtu = 0;

    if (ibRequest.clientibport() > std::numeric_limits<uint8_t>::max() ||
        ibRequest.clientmtu() > std::numeric_limits<uint8_t>::max())
    {
        SET_RESULT(ibResponse, eResponseCode, IBInitResponse::INTERNAL_ERROR);
        return SendMessageToRemoteFinal(clientConnection, ibResponse, requestid);
    }

    bool bRet = IBManager::GetInstance().SetupQPConnections(
        ibRequest.clientlid(),
        static_cast<uint8_t>(ibRequest.clientibport()),
        static_cast<uint8_t>(ibRequest.clientmtu()),
        ibRequest.clientqpn(),
        serverLid,
        serverIbPort,
        serverMtu,
        *ibResponse.mutable_serverqpn());
    if (!bRet)
    {
        LOG_WARN_REQID("Failed to setup qp connections", ibRequest);
        SET_RESULT(ibResponse, eResponseCode, IBInitResponse::INTERNAL_ERROR);
        return SendMessageToRemoteFinal(clientConnection, ibResponse, requestid);
    }

    LOG_DEBUG_REQID("ib init request succeeded={}", ibRequest, serverLid);
    ibResponse.set_serverlid(serverLid);
    ibResponse.set_serveribport(serverIbPort);
    ibResponse.set_servermtu(serverMtu);
    SET_RESULT(ibResponse, eResponseCode, IBInitResponse::SUCCESS);
    return SendMessageToRemoteFinal(clientConnection, ibResponse, requestid);
}