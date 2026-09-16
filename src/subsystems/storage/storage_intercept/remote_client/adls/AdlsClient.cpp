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

#include "AdlsClient.h"
#include "StorageInterceptConfig.h"
#include "AdlsReadChunkApi.h"
#include "AdlsReadDirectoryApi.h"
#include "AdlsGetPropertiesApi.h"
#include "RemoteClientException.h"
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
#define RETURN_IF(condition, x, ...)                               if (condition) { LOG_ERROR(__VA_ARGS__); return x; }
//---------------------------------------------------------------------------------------------------------------------
AdlsClient::AdlsClient(const std::string& mountName)
{
  GetConfig(mountName);
  LOG_DEBUG("Initalized Adls Client succesfully");
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsClient::GetConfig(const std::string& mountName)
{
  m_HostContext = {
    StorageInterceptConfig::GetInstance().GetAdlsHostName(mountName),
    StorageInterceptConfig::GetInstance().GetAdlsClientId(mountName),
    StorageInterceptConfig::GetInstance().GetAdlsClientSecret(mountName),
    StorageInterceptConfig::GetInstance().GetAdlsAuthTokenUrl(mountName),
  };
}
//---------------------------------------------------------------------------------------------------------------------
ssize_t AdlsClient::ReadChunkSync(const std::string& remotePath, void* buf, size_t size, off_t offset)
{
  ssize_t nApiResult = -1;
  AdlsReadChunkApi api(
    m_HostContext,
    remotePath,
    buf,
    size,
    offset);

  api.Execute();
  nApiResult = api.GetResult();
  return nApiResult;
}
//---------------------------------------------------------------------------------------------------------------------
std::string AdlsClient::ReadDirectory(
  const std::string& remotePath,
  const std::string& continuationToken,
  std::vector<DirectoryEntry>& dirEntries)
{
  std::string strApiResult;
  AdlsReadDirectoryApi api(
    m_HostContext,
    remotePath,
    continuationToken,
    dirEntries);

  api.Execute();
  strApiResult = api.GetResult();
  return strApiResult;
}
//---------------------------------------------------------------------------------------------------------------------
int AdlsClient::GetProperties(const std::string& remotePath, FileAttributes& attr)
{
  int nApiResult = -1;
  AdlsGetPropertiesApi api(
    m_HostContext,
    remotePath,
    attr);

  api.Execute();
  nApiResult = api.GetResult();
  return nApiResult;
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsClient::PrepareBlocksForUpload(
  const std::string&,
  size_t)
{
  throw RemoteClientException("Adls backend does not currently have support for write operations");
}
//---------------------------------------------------------------------------------------------------------------------
int AdlsClient::StageBlocksForUpload(
  /*[in]*/ const std::string&,
  /*[in]*/ const void*,
  /*[in]*/ size_t,
  /*[in]*/ off64_t)
{
  throw RemoteClientException("Adls backend does not currently have support for write operations");
}
//---------------------------------------------------------------------------------------------------------------------
int AdlsClient::CommitBlocksForUpload(
  /*[in]*/ const std::string&,
  /*[in]*/ const std::string&)
{
  throw RemoteClientException("Adls backend does not currently have support for write operations");
}
//---------------------------------------------------------------------------------------------------------------------