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
//----------------------------------------------------------------------------------------------------------------------
inline ResponseCode TranslateResponseCode(DeleteResponse_Result deleteResponseCode)
{
    ResponseCode eResponseCode = ResponseCode::NOT_SET;
    switch(deleteResponseCode)
    {
    case DeleteResponse_Result::DeleteResponse_Result_SUCCESS:
        eResponseCode = ResponseCode::SUCCESS;
        break;
    case DeleteResponse_Result::DeleteResponse_Result_INTERNAL_ERROR:
        eResponseCode = ResponseCode::INTERNAL_ERROR;
        break;
    default:
        break;
    }
    return eResponseCode;
}
//---------------------------------------------------------------------------------------------------------------------
bool DeleteFile(IConnection& clientConnection, const cache::DeleteRequest& deleteRequest, enum ResponseCode& eResponseCode);
//---------------------------------------------------------------------------------------------------------------------