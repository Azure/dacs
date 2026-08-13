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
#define SET_RESULT(responseObject, responseVariable, response) \
do { \
    responseObject.set_result(response); \
    responseVariable = TranslateResponseCode(response); \
} while (0);
//---------------------------------------------------------------------------------------------------------------------
enum class ResponseCode
{
    SUCCESS = 0,
    INVALID_TRANSITION = 1,
    BAD_REQUEST = 2,
    INTERNAL_ERROR = 3,
    NETWORK_ERROR = 4,
    NOT_SET = 5,
    END_MARKER =6
};
//----------------------------------------------------------------------------------------------------------------------