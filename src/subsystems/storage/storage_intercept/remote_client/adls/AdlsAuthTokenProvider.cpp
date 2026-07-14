//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "AdlsAuthTokenProvider.h"
#include <chrono>
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
  size_t minExpiryTime = seconds.count() + m_nSafeExpiryWindowSec;

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