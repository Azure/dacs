//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "AdlsClient.h"
#include "StorageInterceptConfig.h"
#include "AdlsReadChunkApi.h"
#include "AdlsReadDirectoryApi.h"
#include "AdlsGetPropertiesApi.h"
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
#define RETURN_IF(condition, x, ...)                               if (condition) { LOG_ERROR(__VA_ARGS__); return x; }
//---------------------------------------------------------------------------------------------------------------------
AdlsClient::AdlsClient()
{
  GetConfig();
  LOG_DEBUG("Initalized Adls Client succesfully");
}
//---------------------------------------------------------------------------------------------------------------------
void AdlsClient::GetConfig()
{
  m_HostContext = {
    StorageInterceptConfig::GetInstance().GetAdlsHostName(),
    StorageInterceptConfig::GetInstance().GetAdlsClientId(),
    StorageInterceptConfig::GetInstance().GetAdlsClientSecret(),
    StorageInterceptConfig::GetInstance().GetAdlsAuthTokenUrl(),
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
