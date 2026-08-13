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
#include "../RemoteClientApiBase.h"
#include "AdlsAuthTokenProvider.h"
#include "AdlsHostContext.h"
#include "AdlsHttpClient.h"
#include <cstdint>
#include <map>
#include <memory>
//---------------------------------------------------------------------------------------------------------------------
class AdlsApiBase : public RemoteClientApiBase
{
  public:
    AdlsApiBase(
      /*[in]*/ const AdlsHostContext& hostContext,
      /*[in]*/ const std::string& operationName)
      : RemoteClientApiBase(operationName)
      , m_hostContext(hostContext)
    {
      
    }

    virtual ~AdlsApiBase() = default;
    
  protected:
    void                                PerformOperation() override;

    /* Method to be overridden by sub-classes with specific operation's implementation logic */
    virtual void                        PerformAdlsOperation() = 0;

  protected:
    std::unique_ptr<AdlsHttpClient>             m_spConnection; /* Http client connection to be used by implementor */
    const AdlsAuthToken&                        GetAuthToken(); /* Get auth token to use with request */

  private:
    void                                        RefreshAuthTokenIfNeeded();
    std::unique_ptr<AdlsHttpClient>             GetAdlsConnection();

  private:
    const AdlsHostContext&                      m_hostContext;
};
//---------------------------------------------------------------------------------------------------------------------