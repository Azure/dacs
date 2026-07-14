//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//--------------------------------------------------------------------------------------------------------------------------------------------
// This file implements the LoggingApi
//--------------------------------------------------------------------------------------------------------------------------------------------
#include "StandardLogger.h"
//-------------------------------------------------------------------------------------------------------------------------------------------
StandardLogger::StandardLogger()
{
    m_bInterceptedApi = false;
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void StandardLogger::Shutdown()
{
    FlushCachedLogInfo();
    Flush();
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void StandardLogger::Flush()
{
    ::fflush(stdout);
    ::fflush(stderr);
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void StandardLogger::LogOutput(std::string logContent)
{
    if(m_bInterceptedApi)
    {

        std::cout << logContent << std::endl;
    }
    else
    {
        // store it in a vector of string and flush it on first interceptthat leads to fork of the server.
        std::lock_guard<std::mutex> guard(m_lock);
        m_strCachedLogInfo.emplace_back(logContent);
    }
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void StandardLogger::LogError(std::string logContent)
{
    std::cerr << logContent << std::endl;
}
//-------------------------------------------------------------------------------------------------------------------------------------------
void StandardLogger::FlushCachedLogInfo()
{
    m_bInterceptedApi = true;
    for (auto itStr : m_strCachedLogInfo)
    {
        std::cout << itStr << std::endl;
    }
    m_strCachedLogInfo.clear();
}
//-------------------------------------------------------------------------------------------------------------------------------------------