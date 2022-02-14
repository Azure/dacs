//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "AdlsAuthToken.h"
#include "AdlsHostContext.h"
#include <memory>
#include <mutex>
//---------------------------------------------------------------------------------------------------------------------
class AdlsAuthTokenProvider
{
  public:
    static AdlsAuthTokenProvider&   GetInstance();
    
  public:
    void                            RefreshAuthTokenIfNeeded(const AdlsHostContext& hostContext);
    AdlsAuthToken&                  GetAuthToken(); /* Get the latest active Adls auth token */
  
  private:
    AdlsAuthTokenProvider() : m_AuthToken()
    {
    }

  private:
    AdlsAuthToken                   m_AuthToken; /* Reflects the latest auth token being used */
    std::mutex                      m_AuthTokenMutex;
    static constexpr size_t         m_nSafeExpiryWindowSec = 5;
};
//---------------------------------------------------------------------------------------------------------------------