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

#include "AdlsAuthTokenProvider.h"
#include <chrono>
#include <limits>
#include <mutex>
#include "AdlsHttpClient.h"
//---------------------------------------------------------------------------------------------------------------------
AdlsAuthTokenProvider& AdlsAuthTokenProvider::GetInstance()
{
  static AdlsAuthTokenProvider m_AuthProvider;
  return m_AuthProvider;
}
//---------------------------------------------------------------------------------------------------------------------
AdlsAuthToken& AdlsAuthTokenProvider::GetAuthToken()
{
  return m_AuthToken;
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsAuthTokenProvider::RefreshAuthTokenIfNeeded(const AdlsHostContext& hostContext)
{
  std::unique_lock<std::mutex> authTokenLock(m_AuthTokenMutex);
  const auto now     = std::chrono::system_clock::now();
  const auto epoch   = now.time_since_epoch();
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
  const auto secondsSinceEpoch = seconds.count();
  if (secondsSinceEpoch < 0 ||
      static_cast<uintmax_t>(secondsSinceEpoch) >
        std::numeric_limits<size_t>::max() - m_nSafeExpiryWindowSec)
  {
    LOG_ERROR("Unable to calculate ADLS token expiry time");
    return;
  }
  const size_t minExpiryTime =
    static_cast<size_t>(secondsSinceEpoch) + m_nSafeExpiryWindowSec;

  if (!m_AuthToken.isTokenValid(minExpiryTime))
  {
    auto m_spConnection = std::make_unique<AdlsHttpClient>(hostContext.authTokenUrl);
    m_spConnection->GetAuthToken(
      hostContext.authTokenUrl,
      hostContext.clientId,
      hostContext.clientSecret,
      m_AuthToken
    );
    LOG_DEBUG("Refreshed the Adls Auth token");
  }
}
//---------------------------------------------------------------------------------------------------------------------