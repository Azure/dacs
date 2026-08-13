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

#include <chrono>
#include <thread>
#include "IRemoteClientException.h"
#include "RemoteClientApiBase.h"
#include "StorageInterceptLogger.h"
//---------------------------------------------------------------------------------------------------------------------
RemoteClientApiBase::RemoteClientApiBase(
  const std::string& operationName,
  uint16_t maxRetryCount,
  uint16_t initialRetrySleepInMs)
  : m_strOperationName(operationName)
  , m_nMaxRetryCount(maxRetryCount)
  , m_nInitialRetrySleepInMs(initialRetrySleepInMs)
{
}
//---------------------------------------------------------------------------------------------------------------------
void RemoteClientApiBase::Execute()
{
  uint16_t retryCount = 0;

  while (retryCount < m_nMaxRetryCount)
  {
    try
    {
      if (retryCount > 0)
      {
        LOG_WARN("Retring remote client operation. operation={} retryCount={}",
          m_strOperationName,
          retryCount);
      }

      PerformOperation();
      return;
    }
    catch(const IRemoteClientException& remoteException)
    {
      if (remoteException.IsRetriableError())
      {
        retryCount++;
        const uint16_t retrySleepInMs = (m_nInitialRetrySleepInMs << (retryCount - 1));

        LOG_WARN("Remote Operation failed with retriable error. operation={} retryCount={} retrySleepInMs={}",
          m_strOperationName,
          retryCount,
          retrySleepInMs);

        std::this_thread::sleep_for(std::chrono::milliseconds(retrySleepInMs));
      }
      else
      {
        LOG_ERROR("Remote Operation failed with non-retriable error. operation={} retryCount={}",
          m_strOperationName,
          retryCount);
        throw remoteException.GetErrorMessage();
      }
    }
  }

  LOG_ERROR("Remote Operation retry limit exhausted. operation={}", m_strOperationName);
  throw "Remote Operation retry limit exhausted";
}
//---------------------------------------------------------------------------------------------------------------------