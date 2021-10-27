//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
// This file implements the LoggingApi
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "LoggingApi.h"
#include <unistd.h>
#include <fcntl.h>
//--------------------------------------------------------------------------------------------------------------------------------------------
thread_local std::string AutoOperationIdTLS::OperationIdTLS{};
//--------------------------------------------------------------------------------------------------------------------------------------------
bool LoggingApi::m_bInitAsync{false};
bool LoggingApi::m_bShutdownLogging{false};
int LoggingApi::m_nOwnerId{-1};
pid_t LoggingApi::m_nCachedPid{INVALID_PID};
std::atomic<bool> LoggingApi::m_bForkFlag{false};
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Init()
{
    Shutdown();

    m_bShutdownLogging = false;

    if(!m_bInitAsync)
    {
        m_bInitAsync = false;

        if(m_bInitAsync)
        {
            // Enable logging to console. GLOG only allows to stderr.
            // FLAGS_logtostderr = true; //  TODO: Resolve demangle error when enabling glog back.

            google::InitGoogleLogging("DACS");
            LOG_INFO("Initialized GLOG.");
        }
        else
        {
            LOG_INFO("Initialized logging.");
        }
    }

    pthread_atfork(LoggingApi::Prepare, LoggingApi::Parent, LoggingApi::Child);
};
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Shutdown()
{
    Flush();

    if(m_bInitAsync)
    {
        LOG_INFO("Shutting down GLOG.");
        m_bInitAsync = false;
        google::ShutdownGoogleLogging();
    }

    LOG_INFO("LoggingApi::Shutdown()");
    m_bShutdownLogging = true;
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Flush()
{
    ::fflush(stdout);
    ::fflush(stderr);
    if(m_bInitAsync)
    {
        LOG_INFO("Flushing GLOG logs.");
        // From GLOG header documentation:
        // const int GLOG_INFO = 0, GLOG_WARNING = 1, GLOG_ERROR = 2, GLOG_FATAL = 3,
        // Flushes all log files that contains messages that are at least of
        // the specified severity level.  Thread-safe.
        // In this case we will flush INFO, WARNING, ERROR and FATAL (if any)
        google::FlushLogFiles(google::GLOG_INFO);
    }
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