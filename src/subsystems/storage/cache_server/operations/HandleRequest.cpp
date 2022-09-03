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
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
using namespace cache;
//---------------------------------------------------------------------------------------------------------------------
bool HandleRequest(IConnection& clientConnection)
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
    if (request.type_case() == Request::kDownloadrequest)
    {
        bRet = DownloadFile(clientConnection, request.downloadrequest());
    }
    else if (request.type_case() == Request::kUploadrequest)
    {
        bRet = UploadFile(clientConnection, request.uploadrequest());
    } 
    else if (request.type_case() == Request::kLockrequest)
    {
        bRet = LockFile(clientConnection, request.lockrequest());
    }
    else if (request.type_case() == Request::kGetattributerequest)
    {
        bRet = HandleGetAttribute(clientConnection, request.getattributerequest());
    }
    else if (request.type_case() == Request::kPutattributerequest)
    {
        bRet = HandlePutAttribute(clientConnection, request.putattributerequest());
    }
    else if (request.type_case() == Request::kDeleterequest)
    {
        bRet = DeleteFile(clientConnection, request.deleterequest());
    }
    else if (request.type_case() == Request::kIbinitrequest)
    {
        bRet = IBInitializeClient(clientConnection, request.ibinitrequest());
    }
    else
    {
        LOG_ERROR("unkown req type={}, close id={}", request.type_case(), clientConnection.GetId());
    }

    LOG_DEBUG("Done handling request={}", request.type_case());
    return bRet;
}
//---------------------------------------------------------------------------------------------------------------------