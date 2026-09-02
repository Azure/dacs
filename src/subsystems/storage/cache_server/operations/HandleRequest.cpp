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

#include "Constants.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "UploadRequest.h"
#include "DownloadRequest.h"
#include "DeleteRequest.h"
#include "IBInitRequest.h"
#include "GetCacheLastAccessTimeDistributionRequest.h"
#include "IConnection.h"
#include "cache.pb.h"
#include "Logger.h"
#include "HandleRequest.h"
#include "GetCacheServers.h"
#include <limits>
#include "LocatorRequest.h"
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
bool HandleRequest(IConnection& clientConnection,
                   Request::TypeCase& eRequestType,
                   enum ResponseCode& eResponseCode,
                   std::size_t& filesize)
{
    char metaData[REQUEST_METADATA_SIZE];
    int res = ReadBytesToArray(clientConnection, metaData, REQUEST_METADATA_SIZE);
    if (res != REQUEST_METADATA_SIZE)
    {
        LOG_WARN("Failed to read request metadata from client, id={}", clientConnection.GetId());
        return false;
    }

    const uint64_t protoSizeValue = ConvertBytesToInt(metaData, REQUEST_METADATA_SIZE);
    if (protoSizeValue > MAX_REQUEST_PROTO_SIZE ||
        protoSizeValue > static_cast<uint64_t>(std::numeric_limits<int>::max()))
    {
        LOG_WARN("Rejecting request with invalid protobuf size, id={}, size={}, max={}",
            clientConnection.GetId(), protoSizeValue, MAX_REQUEST_PROTO_SIZE);
        return false;
    }
    const uint32_t protoSize = static_cast<uint32_t>(protoSizeValue);

    string requestMessage(protoSize, 0);
    res = ReadBytesToArray(clientConnection, requestMessage.data(), static_cast<int>(protoSize));
    if (res != static_cast<int>(protoSize))
    {
        LOG_WARN("Failed to read protobuf message, id={}, size={}", clientConnection.GetId(), protoSize);
        return false;
    }

    Request request;
    if (!request.ParseFromString(requestMessage))
    {
        LOG_WARN("Failed to parse protobuf message, id={}, size={}", clientConnection.GetId(), protoSize);
        return false;
    }

    bool bRet = false;
    eRequestType = request.type_case();
    if (eRequestType == Request::kDownloadrequest)
    {
        bRet = DownloadFile(clientConnection, request.downloadrequest(), eResponseCode, filesize);
    }
    else if (eRequestType == Request::kUploadrequest)
    {
        bRet = UploadFile(clientConnection, request.uploadrequest(), eResponseCode, filesize);
    } 
    else if (eRequestType == Request::kDeleterequest)
    {
        bRet = DeleteFile(clientConnection, request.deleterequest(), eResponseCode);
    }
    else if (eRequestType == Request::kIbinitrequest)
    {
        bRet = IBInitializeClient(clientConnection, request.ibinitrequest(), eResponseCode);
    }
    else if (eRequestType == Request::kGetcachelastaccesstimedistributionrequest)
    {
        bRet = GetLastAccessTimeDistribution(clientConnection, request.getcachelastaccesstimedistributionrequest(), eResponseCode);
    }
    else if (eRequestType == Request::kGetcacheserversrequest)
    {
        bRet = GetCacheServers(clientConnection, request.getcacheserversrequest(), eResponseCode);
    }
    else if (eRequestType == Request::kLocatorregisterrequest)
    {
        bRet = HandleLocatorRegister(clientConnection, request.locatorregisterrequest());
    }
    else if (eRequestType == Request::kLocatorlookuprequest)
    {
        bRet = HandleLocatorLookup(clientConnection, request.locatorlookuprequest());
    }
    else if (eRequestType == Request::kLocatorrefreshrequest)
    {
        bRet = HandleLocatorRefresh(clientConnection, request.locatorrefreshrequest());
    }
    else if (eRequestType == Request::kLocatorunregisterrequest)
    {
        bRet = HandleLocatorUnregister(clientConnection, request.locatorunregisterrequest());
    }
    else if (eRequestType == Request::kLocatorprefixlookuprequest)
    {
        bRet = HandleLocatorPrefixLookup(clientConnection, request.locatorprefixlookuprequest());
    }
    else if (eRequestType == Request::kLocatorregisterchainrequest)
    {
        bRet = HandleLocatorRegisterChain(clientConnection, request.locatorregisterchainrequest());
    }
    else
    {
        LOG_ERROR("unkown req type={}, close id={}", request.type_case(), clientConnection.GetId());
    }

    LOG_DEBUG("Done handling request={}", request.type_case());
    return bRet;
}
//---------------------------------------------------------------------------------------------------------------------