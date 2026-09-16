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

#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <unordered_map>
#include <mutex>

#include "StorageInterceptLogger.h"
//---------------------------------------------------------------------------------------------------------------------
// Appending a generated GUID at the end of the marker to avoid collisions with actual markers
static const std::string FAILED_MARKER = "READDIR_ERROR_5601fce891d847d0aa58b361ab9a45be";
//---------------------------------------------------------------------------------------------------------------------
// Class provides a general mechanism for reporting any errors that occur along the interception path back to the calling application
// Exposes two main APIs - set error log and get last error
// Design decision: Going with a singleton instance with static lifetime that can be accessed by any component in the storage interceptor
// for a few reasons:
// 1. Error handler can persist across forks. Unlike the cache client/remote clients where connections must be re-created after forks,
// the child process can just invoke the ResetState() API of the error handler
// 2. Error handler needs to be accessible from both ContentHandler and RemoteClientProvider. Singleton instance helps avoid dependency
// injection along ContentHandler->StreamingClient->RemoteClientProvider path
class ErrorHandler
{
  public:
    // Singleton instance
    static ErrorHandler& GetInstance();

    void ResetState();
    void SetError(/*[in]*/ const std::string& error, /*[in]*/ const pid_t tid = static_cast<pid_t>(::gettid()));
    char* GetLastError(/*[in]*/ const pid_t tid = static_cast<pid_t>(::gettid()));
    bool IsErrorSet(/*[in]*/ const pid_t tid = static_cast<pid_t>(::gettid()));

  private:
    ErrorHandler();

    std::mutex m_lock;
    std::unordered_map<pid_t, std::string> m_mapTidToErrorString;
};
//---------------------------------------------------------------------------------------------------------------------