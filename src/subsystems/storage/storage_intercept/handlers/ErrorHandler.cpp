//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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
void ErrorHandler::SetError(/*[in]*/ const std::string& error, /*[in]*/ const int tid)
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
char* ErrorHandler::GetLastError(/*[in]*/ const int tid)
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
bool ErrorHandler::IsErrorSet(/*[in]*/ const int tid)
{
    std::lock_guard<std::mutex> lock(m_lock);

    return m_mapTidToErrorString.find(tid) != m_mapTidToErrorString.end();
}
//---------------------------------------------------------------------------------------------------------------------