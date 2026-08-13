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

#include "CurlConnectionManager.h"
#include "CurlClientException.h"
//---------------------------------------------------------------------------------------------------------------------
#define WARN_IF(condition, ...)                                           { if (condition) { LOG_WARN(__VA_ARGS__); } }
//---------------------------------------------------------------------------------------------------------------------
size_t CurlConnectionManager::m_nMaxHandles = 100UL;
//---------------------------------------------------------------------------------------------------------------------
CurlConnectionManager& CurlConnectionManager::GetInstance()
{
  static CurlConnectionManager m_ConnectionManager;
  return m_ConnectionManager;
}
//---------------------------------------------------------------------------------------------------------------------
CurlConnectionManager::~CurlConnectionManager()
{
  std::unique_lock<std::mutex> connPoolLock(m_connectionPoolLock);
  while(m_queueHandles.size() > 0)
  {
    m_queueHandles.pop();
  }
}
//---------------------------------------------------------------------------------------------------------------------
std::unique_ptr<CurlHandle> CurlConnectionManager::GetCurlHandle()
{
  {
    std::unique_lock<std::mutex> connPoolLock(m_connectionPoolLock);
    if (m_queueHandles.size() > 0)
    {
      std::unique_ptr<CurlHandle> result = std::move(m_queueHandles.front());
      m_queueHandles.pop();
      return result;
    }
  }
  // BTW, this doesn't guard against calling libcurl init from other curl-based libs.
  static std::once_flag curlInitFlag;

  std::call_once(
    curlInitFlag,
    []
    {
      if (curl_global_init(CURL_GLOBAL_ALL) != 0)
      {
        throw CurlClientException("libcurl init failed");
      }
    });
  
  std::unique_ptr<CurlHandle> curlHandle = std::make_unique<CurlHandle>();
  if (curlHandle.get() == nullptr)
  {
    throw CurlClientException("libcurl easy object creation failed");
  }
  return curlHandle;
}
//---------------------------------------------------------------------------------------------------------------------
void CurlConnectionManager::ReleaseCurlHandle(std::unique_ptr<CurlHandle>&& spCurlHandle)
{
  size_t currPoolSize = 0;
  {
    std::unique_lock<std::mutex> connPoolLock(m_connectionPoolLock);
    m_queueHandles.push(std::move(spCurlHandle));
    currPoolSize = m_queueHandles.size();
  }
  WARN_IF(
    currPoolSize >= CurlConnectionManager::m_nMaxHandles,
    "Warning, too many alive connections: {}, max connections expected: {}", currPoolSize, m_nMaxHandles
  );
}
//---------------------------------------------------------------------------------------------------------------------