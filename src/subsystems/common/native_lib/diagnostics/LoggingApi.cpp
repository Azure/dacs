//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
// This file implements the LoggingApi
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "LoggingApi.h"
//--------------------------------------------------------------------------------------------------------------------------------------------
thread_local std::string AutoOperationIdTLS::OperationIdTLS{};
//--------------------------------------------------------------------------------------------------------------------------------------------
bool LoggingApi::bInitAsync{false};
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Init()
{
    Shutdown();

    if(!bInitAsync)
    {
        bInitAsync = false;
        // Enable logging to console. GLOG only allows to stderr.
        FLAGS_logtostderr = true;

        if(bInitAsync)
        {
            google::InitGoogleLogging("DACS");
            LOG_INFO("Initialized GLOG.");
        }
        else
        {
            LOG_INFO("Initialized logging without GLOG.");
        }
    }
};
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Shutdown()
{
    Flush();

    if(bInitAsync)
    {
        LOG_INFO("Shutting down GLOG.");
        bInitAsync = false;
        google::ShutdownGoogleLogging();
    }
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void LoggingApi::Flush()
{
    if(bInitAsync)
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