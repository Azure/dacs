///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "Constants.h"
#include "Utils.h"
#include "ConnectionUtils.h"
#include "UploadRequest.h"
#include "LockRequest.h"
#include "DownloadRequest.h"
#include "PutAttributeRequest.h"
#include "GetAttributeRequest.h"
#include "DeleteRequest.h"
#include "IBInitRequest.h"
#include "IConnection.h"
#include "cache.pb.h"
#include "Logger.h"
#include "HandleRequest.h"
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

    uint32_t protoSize = ConvertBytesToInt(metaData, REQUEST_METADATA_SIZE);
    string requestMessage(protoSize, 0);
    res = ReadBytesToArray(clientConnection, requestMessage.data(), protoSize);
    if (res != protoSize)
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
    else if (eRequestType == Request::kLockrequest)
    {
        bRet = LockFile(clientConnection, request.lockrequest());
    }
    else if (eRequestType == Request::kGetattributerequest)
    {
        bRet = HandleGetAttribute(clientConnection, request.getattributerequest());
    }
    else if (eRequestType == Request::kPutattributerequest)
    {
        bRet = HandlePutAttribute(clientConnection, request.putattributerequest());
    }
    else if (eRequestType == Request::kDeleterequest)
    {
        bRet = DeleteFile(clientConnection, request.deleterequest(), eResponseCode);
    }
    else if (eRequestType == Request::kIbinitrequest)
    {
        bRet = IBInitializeClient(clientConnection, request.ibinitrequest(), eResponseCode);
    }
    else
    {
        LOG_ERROR("unkown req type={}, close id={}", request.type_case(), clientConnection.GetId());
    }

    LOG_DEBUG("Done handling request={}", request.type_case());
    return bRet;
}
//---------------------------------------------------------------------------------------------------------------------