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

// This file implements the LoggingApi
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "LoggingApi.h"
#include <fcntl.h>
//--------------------------------------------------------------------------------------------------------------------------------------------
thread_local std::string AutoOperationIdTLS::OperationIdTLS{};
//--------------------------------------------------------------------------------------------------------------------------------------------
int LoggingApi::m_nOwnerId{-1};
pid_t LoggingApi::m_nCachedPid{INVALID_PID};
thread_local pid_t LoggingApi::m_nCachedTidTLS{INVALID_TID};
std::atomic<bool> LoggingApi::m_bForkFlag{false};
std::shared_ptr<ILogger> LoggingApi::m_upLogger;
std::atomic<bool> LoggingApi::m_bIsInitialized{false};
std::atomic<bool> LoggingApi::m_bIsInitializing{false};
std::recursive_mutex LoggingApi::m_lock;
int LoggingApi::m_nRuntimeLogLevel{static_cast<int>(LOG_LEVEL)};

namespace {
int ParseLogLevel(const char* s, int fallback)
{
    if (!s || !*s) return fallback;
    std::string v(s);
    for (auto& c : v) c = static_cast<char>(::tolower(c));
    if (v == "critical" || v == "fatal") return static_cast<int>(LogLevel::Critical);
    if (v == "error" || v == "err") return static_cast<int>(LogLevel::Error);
    if (v == "warn" || v == "warning") return static_cast<int>(LogLevel::Warn);
    if (v == "info") return static_cast<int>(LogLevel::Info);
    if (v == "debug" || v == "dbg") return static_cast<int>(LogLevel::Debug);
    if (v == "trace" || v == "trc") return static_cast<int>(LogLevel::Trace);
    // numeric passthrough
    try {
        int n = std::stoi(v);
        if (n >= static_cast<int>(LogLevel::Critical) && n <= static_cast<int>(LogLevel::Trace)) return n;
    } catch (const std::invalid_argument&) {
        printf("Invalid STORAGE_LOG_LEVEL value: %s defauling to %d\n", s, fallback);
    } catch (const std::out_of_range&) {
        printf("Out of range STORAGE_LOG_LEVEL value: %s defauling to %d\n", s, fallback);
    }

    return fallback;
}
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Init(/*[in]*/ bool bIgnoreRankInFileName)
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);

    //prevent multiple initialization coming in.
    if(m_bIsInitialized || m_bIsInitializing)
    {
        return;
    }

    m_bIsInitializing = true;
    
    char* fileStandardOutput = std::getenv("DACS_LOG_OUTPUT_FILE");
    char* fileErrorOutput = std::getenv("DACS_LOG_ERROR_FILE");
    
    char* fileStandardOutputOverride = std::getenv("DACS_LOG_OUTPUT_FILE_OVERRIDE");
    char* fileErrorOutputOverride = std::getenv("DACS_LOG_ERROR_FILE_OVERRIDE");

    fileStandardOutput = fileStandardOutputOverride ? fileStandardOutputOverride : fileStandardOutput;
    fileErrorOutput = fileErrorOutputOverride ? fileErrorOutputOverride : fileErrorOutput;

    // Enable file logger when the issue is fixed
    if(!fileStandardOutput || !fileErrorOutput)
    {
        m_upLogger = std::make_shared<StandardLogger>();
    }
    else
    {
        m_upLogger = std::make_shared<FileLogger>(fileStandardOutput, fileErrorOutput, bIgnoreRankInFileName);
    }

    m_upLogger->Flush();
    pthread_atfork(LoggingApi::Prepare, LoggingApi::Parent, LoggingApi::Child);

    // Pick up runtime log level from env if provided
    // Note: This only relaxes/filters at runtime within the compile-time cap (LOG_LEVEL).
    if (const char* lvl = std::getenv("STORAGE_LOG_LEVEL"))
    {
        m_nRuntimeLogLevel = static_cast<int>(ParseLogLevel(lvl, m_nRuntimeLogLevel));
    }

    m_bIsInitialized = true;
    m_bIsInitializing = false;
};
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Shutdown()
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);

    if(!m_bIsInitialized)
    {
        return;
    }

    Flush();

    m_bIsInitialized = false;
    m_upLogger->Shutdown();
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Flush()
{ 
    std::lock_guard<std::recursive_mutex> lock(m_lock);

    if(!m_bIsInitialized)
    {
        return;
    }

    m_upLogger->Flush();
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::SetOwnerId(/*[in]*/ int nOwnerId)
{
    // Intentionally left non thread safe. The caller is expected to guarantee this if needed.
    m_nOwnerId = nOwnerId;
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Prepare()
{
    m_bForkFlag = true;
    m_nCachedPid = INVALID_PID;
    m_nCachedTidTLS = INVALID_TID;
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Parent()
{
    m_nCachedPid = ::getpid();
    m_bForkFlag = false;
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Child()
{
    m_nCachedPid = ::getpid();
    m_bForkFlag = false;
}
//-------------------------------------------------------------------------------------------------------------------------------------------
pid_t LoggingApi::Getpid()
{
    pid_t ret = INVALID_PID;
    if (m_nCachedPid == INVALID_PID)
    {
        ret = ::getpid();

        if (m_bForkFlag == false)
        {
            m_nCachedPid = ret;
        }
    }
    else 
    {
        ret = m_nCachedPid;
    }

    return ret;
}
//-------------------------------------------------------------------------------------------------------------------------------------------
pid_t LoggingApi::Gettid()
{
    if(m_nCachedTidTLS == INVALID_TID)
    {
        // SYS_gettid returns long, but Linux thread IDs use the pid_t domain.
        m_nCachedTidTLS = static_cast<pid_t>(::gettid());
    }

    return m_nCachedTidTLS;
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::FlushCachedLog()
{
    m_upLogger->FlushCachedLogInfo();
}
//---------------------------------------------------------------------------------------------------------------------