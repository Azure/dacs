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
// Adapts DACS BlobAuthTokenProvider (MSI / workload-identity / Azure Identity SDK) to the
// azure-sdk-for-cpp TokenCredential interface.
//---------------------------------------------------------------------------------------------------------------------
#include "BlobAuthTokenProvider.h"
#include "StorageInterceptConfig.h"

#include <azure/core/credentials/credentials.hpp>
#include <azure/core/datetime.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
//---------------------------------------------------------------------------------------------------------------------
class DacsTokenCredential final : public Azure::Core::Credentials::TokenCredential
{
    public:
        explicit DacsTokenCredential(
            std::string mountName,
            BlobAuthTokenProvider& provider = BlobAuthTokenProvider::GetInstance())
            : Azure::Core::Credentials::TokenCredential("DacsTokenCredential")
            , m_strMountName(std::move(mountName))
            , m_provider(provider)
            , m_tokenRefreshBufferSeconds(
                  StorageInterceptConfig::GetInstance().GetTokenRefreshBufferInSeconds())
        {
        }

        Azure::Core::Credentials::AccessToken GetToken(
            /*[in]*/ const Azure::Core::Credentials::TokenRequestContext& /*tokenRequestContext*/,
            /*[in]*/ const Azure::Core::Context& /*context*/) const override
        {
            const TokenInfo tokenInfo = m_provider.GetAccessTokenInfo(m_strMountName);

            Azure::Core::Credentials::AccessToken accessToken;
            accessToken.Token = tokenInfo.token;

            // Cap ExpiresOn at (now + refreshBuffer) so the SDK refreshes around when the token enters
            // the provider's refresh window, but never past the token's real expiry (the min).
            accessToken.ExpiresOn = Azure::DateTime(std::min(
                std::chrono::system_clock::now() + std::chrono::seconds(m_tokenRefreshBufferSeconds),
                tokenInfo.expiryTime));
            return accessToken;
        }

    private:
        const std::string      m_strMountName;
        BlobAuthTokenProvider& m_provider;
        const uint32_t         m_tokenRefreshBufferSeconds;
};
//---------------------------------------------------------------------------------------------------------------------
