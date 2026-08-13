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