///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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