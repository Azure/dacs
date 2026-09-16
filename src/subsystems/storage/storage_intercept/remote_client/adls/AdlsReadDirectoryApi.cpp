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

#include "AdlsReadDirectoryApi.h"
#include "AdlsHttpClientException.h"
#include "AdlsClientException.h"
#include "AdlsHostContext.h"
#include <iostream>
//---------------------------------------------------------------------------------------------------------------------
AdlsReadDirectoryApi::AdlsReadDirectoryApi(
  const AdlsHostContext& hostContext,
  const std::string& remotePath,
  const std::string& continuationToken,
  std::vector<DirectoryEntry>& dirEntries)
  : AdlsApiBase(
      hostContext,
      "AdlsReadDirectoryApi")
  , m_strRemotePath(remotePath)
  , m_strContinuationToken(continuationToken)
  , m_vecDirEntries(dirEntries)
  , m_strResultContinuationToken("")
{
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsReadDirectoryApi::PerformAdlsOperation()
{
  try
  {
    m_strResultContinuationToken = 
      m_spConnection->ListDirectory(
        GetAuthToken(),
        m_strRemotePath,
        m_strContinuationToken,
        m_vecDirEntries);
  }
  catch(const AdlsHttpClientException& e)
  {
    LOG_ERROR("Unable to list directory for the given path: {}", m_strRemotePath);
    throw AdlsClientException(e);
  }
}
//---------------------------------------------------------------------------------------------------------------------
std::string AdlsReadDirectoryApi::GetResult() const
{
  return m_strResultContinuationToken;
}
//---------------------------------------------------------------------------------------------------------------------