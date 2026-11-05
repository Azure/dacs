// Copyright (c) Microsoft Corporation.
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
#include <cstdint>
#include <string_view>
//---------------------------------------------------------------------------------------------------------------------
namespace blobauthretry
{
inline bool IsAuthorizationPermissionMismatch(int statusCode, std::string_view errorCode)
{
    return statusCode == 403 && errorCode == "AuthorizationPermissionMismatch";
}

inline bool ShouldRetry(
    int statusCode,
    std::string_view errorCode,
    bool hasRefreshableCredential,
    uint32_t attempt,
    uint32_t maxAttempts)
{
    return hasRefreshableCredential
        && IsAuthorizationPermissionMismatch(statusCode, errorCode)
        && attempt < maxAttempts;
}
}
//---------------------------------------------------------------------------------------------------------------------
