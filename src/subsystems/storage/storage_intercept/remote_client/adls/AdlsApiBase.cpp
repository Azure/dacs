//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "AdlsApiBase.h"
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<AdlsHttpClient> AdlsApiBase::GetAdlsConnection()
{
  auto spConnection = std::make_unique<AdlsHttpClient>(m_hostContext.hostName);
  return spConnection;
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsApiBase::PerformOperation()
{
  /* Refresh auth token before the API operation, if required */
  RefreshAuthTokenIfNeeded();

  /* Get a fresh ADLS connection to use for sending remote request */
  m_spConnection = std::move(GetAdlsConnection());

  /* Perform this API operation, needs to be overridden by the respective api sub-classes */
  PerformAdlsOperation();
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsApiBase::RefreshAuthTokenIfNeeded()
{
  AdlsAuthTokenProvider::GetInstance().RefreshAuthTokenIfNeeded(m_hostContext);
}
//---------------------------------------------------------------------------------------------------------------------
const AdlsAuthToken& AdlsApiBase::GetAuthToken()
{
  return AdlsAuthTokenProvider::GetInstance().GetAuthToken();
}
//---------------------------------------------------------------------------------------------------------------------