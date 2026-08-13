/// Copyright (c) Microsoft Corporation.
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

#include "AdlsGetPropertiesApi.h"
#include "AdlsHttpClientException.h"
#include "AdlsClientException.h"
#include "AdlsHostContext.h"
#include <iostream>
//---------------------------------------------------------------------------------------------------------------------
AdlsGetPropertiesApi::AdlsGetPropertiesApi(
  const AdlsHostContext& hostContext,
  const std::string& filePath,
  FileAttributes& attr)
  : AdlsApiBase(
      hostContext,
      "AdlsGetPropertiesApi")
  , m_strFilePath(filePath)
  , m_Attr(attr)
  , m_nResult(-1)
{
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsGetPropertiesApi::PerformAdlsOperation()
{
  try
  {
    m_nResult = m_spConnection->GetFileOrDirectoryStatus(GetAuthToken(), m_strFilePath, m_Attr);
  }
  catch(const AdlsHttpClientException& e)
  {
    LOG_ERROR("Error getting properties for remote path: {}", m_strFilePath);
    throw AdlsClientException(e);
  }
}
//---------------------------------------------------------------------------------------------------------------------
int AdlsGetPropertiesApi::GetResult() const
{
  return m_nResult;
}
//---------------------------------------------------------------------------------------------------------------------
