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

#include "ErrorHandler.h"

#include <stdlib.h>
#include <string.h>
//---------------------------------------------------------------------------------------------------------------------
ErrorHandler& ErrorHandler::GetInstance()
{
    static ErrorHandler s_instance;
    return s_instance;
}
//---------------------------------------------------------------------------------------------------------------------
ErrorHandler::ErrorHandler()
{}
//---------------------------------------------------------------------------------------------------------------------
void ErrorHandler::ResetState()
{
    std::lock_guard<std::mutex> lock(m_lock);

    m_mapTidToErrorString.clear();
}
//---------------------------------------------------------------------------------------------------------------------
void ErrorHandler::SetError(/*[in]*/ const std::string& error, /*[in]*/ const pid_t tid)
{
    if(error.empty())
    {
        LOG_WARN("Skipping setting error, string is empty");
        return;
    }

    std::lock_guard<std::mutex> lock(m_lock);

    LOG_DEBUG("Setting error to {}", error);
    auto it = m_mapTidToErrorString.find(tid);
    if(it == m_mapTidToErrorString.end())
    {
        m_mapTidToErrorString.emplace(tid, error);
    }
    else
    {
        it->second = error;
    }
}
//---------------------------------------------------------------------------------------------------------------------
char* ErrorHandler::GetLastError(/*[in]*/ const pid_t tid)
{
    std::lock_guard<std::mutex> lock(m_lock);
    char* error = nullptr;

    auto it = m_mapTidToErrorString.find(tid);
    if(it != m_mapTidToErrorString.end())
    {
        error = (char*)::malloc(it->second.size() + 1);
        ::strcpy(error, it->second.c_str());
    }

    return error;
}
//---------------------------------------------------------------------------------------------------------------------
bool ErrorHandler::IsErrorSet(/*[in]*/ const pid_t tid)
{
    std::lock_guard<std::mutex> lock(m_lock);

    return m_mapTidToErrorString.find(tid) != m_mapTidToErrorString.end();
}
//---------------------------------------------------------------------------------------------------------------------