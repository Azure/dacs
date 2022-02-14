//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "../RemoteClientApiBase.h"
#include "AdlsAuthTokenProvider.h"
#include "AdlsHostContext.h"
#include "AdlsHttpClient.h"
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