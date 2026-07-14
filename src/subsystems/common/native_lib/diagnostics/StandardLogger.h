//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "ILogger.h"
#include <vector>
#include <atomic>
#include <mutex>

class StandardLogger: public ILogger
{
public:
    StandardLogger();
    void Shutdown();
    void Flush();
    void LogOutput(std::string logContent);
    void LogError(std::string logContent);
    void FlushCachedLogInfo();

private:
    std::atomic<bool> m_bInterceptedApi;
    std::vector<std::string> m_strCachedLogInfo;
    std::mutex m_lock;
};