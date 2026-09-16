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
#include <limits>
#include <thread>
#include "IRemoteClientException.h"
#include "RemoteClientException.h"
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
        const unsigned shift = static_cast<unsigned>(retryCount - 1);
        const uint64_t retrySleepCandidate = shift < 64
          ? static_cast<uint64_t>(m_nInitialRetrySleepInMs) << shift
          : std::numeric_limits<uint64_t>::max();
        const uint16_t retrySleepInMs = static_cast<uint16_t>(std::min(
          retrySleepCandidate,
          static_cast<uint64_t>(std::numeric_limits<uint16_t>::max())));

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
        // Re-throw as a *typed* RemoteClientException (a std::exception)
        // instead of a bare `std::string`. The C API boundary that generic
        // Rust callers use -- StorageInterceptorImpl::SI_open/SI_pread/SI_close
        // -- historically caught only `RemoteClientException`. A bare
        // `throw std::string` (or the `const char*` below) therefore slipped
        // past that catch, unwound out of the extern "C" SI_* frames, and
        // crossed into Rust, whose catch_unwind cannot handle a foreign C++
        // exception and calls abort() ("Rust cannot catch foreign exceptions,
        // aborting"). Throwing a typed exception lets the boundary convert the
        // failure into a clean, retriable error string instead of crashing the
        // whole process (e.g. a GB200 training job).
        throw RemoteClientException(remoteException.GetErrorMessage());
      }
    }
  }

  LOG_ERROR("Remote Operation retry limit exhausted. operation={}", m_strOperationName);
  // Never throw a bare `const char*` across the C FFI boundary -- wrap it in
  // a typed RemoteClientException (see note above). This is the
  // retry-exhaustion path that a heavily-loaded, highly concurrent reader
  // (e.g. 16 GPUs sharing one cache server) is most likely to hit.
  throw RemoteClientException("Remote Operation retry limit exhausted");
}
//---------------------------------------------------------------------------------------------------------------------