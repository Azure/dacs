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