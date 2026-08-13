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