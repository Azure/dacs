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

#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include "cache.pb.h"
#include "IConnection.h"
#include "ResponseCode.h"

using namespace cache;
//-----------------------------------------------------------------------------------------------------------------------
inline ResponseCode TranslateResponseCode(IBInitResponse_Result ibInitResponseCode)
{
    ResponseCode eResponseCode = ResponseCode::NOT_SET;
    switch(ibInitResponseCode)
    {
        case IBInitResponse_Result::IBInitResponse_Result_SUCCESS:
            eResponseCode = ResponseCode::SUCCESS;
            break;
        case IBInitResponse_Result::IBInitResponse_Result_AUTH_FAILED:
        case IBInitResponse_Result::IBInitResponse_Result_IB_NOT_SUPPORTED:
            eResponseCode = ResponseCode::BAD_REQUEST;
            break;
        case IBInitResponse_Result::IBInitResponse_Result_INTERNAL_ERROR:
            eResponseCode = ResponseCode::INTERNAL_ERROR;
            break;
        default:
            break;
    }

    return eResponseCode;
}
//---------------------------------------------------------------------------------------------------------------------
bool IBInitializeClient(IConnection& clientConnection, const cache::IBInitRequest& ibInitRequest, enum ResponseCode& eResponseCode);
//---------------------------------------------------------------------------------------------------------------------