//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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
    void SetError(/*[in]*/ const std::string& error, /*[in]*/ const int tid = ::gettid());
    char* GetLastError(/*[in]*/ const int tid = ::gettid());
    bool IsErrorSet(/*[in]*/ const int tid = ::gettid());

  private:
    ErrorHandler();

    std::mutex m_lock;
    std::unordered_map<int, std::string> m_mapTidToErrorString;
};
//---------------------------------------------------------------------------------------------------------------------