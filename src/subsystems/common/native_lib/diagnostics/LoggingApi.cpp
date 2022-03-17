//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
// This file implements the LoggingApi
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "LoggingApi.h"
#include <fcntl.h>
//--------------------------------------------------------------------------------------------------------------------------------------------
thread_local std::string AutoOperationIdTLS::OperationIdTLS{};
//--------------------------------------------------------------------------------------------------------------------------------------------
int LoggingApi::m_nOwnerId{-1};
pid_t LoggingApi::m_nCachedPid{INVALID_PID};
std::atomic<bool> LoggingApi::m_bForkFlag{false};
std::shared_ptr<ILogger> LoggingApi::m_upLogger;
std::atomic<bool> LoggingApi::m_bIsInitialized{false};
std::atomic<bool> LoggingApi::m_bIsInitializing{false};
std::recursive_mutex LoggingApi::m_lock;
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Init()
{
    std::lock_guard<std::recursive_mutex> lock(m_lock);

    //prevent multiple initialization coming in.
    if(m_bIsInitialized || m_bIsInitializing)
    {
        return;
    }

    m_bIsInitializing = true;
    
    char* fileStandardOutput = std::getenv("DACS_LOG_OUTPUT_FLIE");
    char* fileErrorOutput = std::getenv("DACS_LOG_ERROR_FILE");

    if(!fileStandardOutput || !fileErrorOutput)
    {
        m_upLogger = std::make_shared<StandardLogger>();
    }
    else
    {
        m_upLogger = std::make_shared<FileLogger>(fileStandardOutput, fileErrorOutput);
    }

    m_upLogger->Flush();
    pthread_atfork(LoggingApi::Prepare, LoggingApi::Parent, LoggingApi::Child);

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