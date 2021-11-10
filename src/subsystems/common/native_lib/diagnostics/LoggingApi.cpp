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
bool LoggingApi::m_bShutdownLogging{false};
int LoggingApi::m_nOwnerId{-1};
pid_t LoggingApi::m_nCachedPid{INVALID_PID};
std::atomic<bool> LoggingApi::m_bForkFlag{false};
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Init()
{
    Shutdown();

    m_bShutdownLogging = false;
    LOG_INFO("Initialized logging.");

    pthread_atfork(LoggingApi::Prepare, LoggingApi::Parent, LoggingApi::Child);
};
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Shutdown()
{
    Flush();

    LOG_INFO("LoggingApi::Shutdown()");
    m_bShutdownLogging = true;
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Flush()
{
    ::fflush(stdout);
    ::fflush(stderr);
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