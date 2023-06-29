//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "ErrorHandler.h"
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
SetErrorResponse ErrorHandler::SetError(/*[in]*/ const std::string& error, /*[in]*/ const int key)
{
    if(error.empty())
    {
        LOG_ERROR("Error string is empty");
        return SetErrorResponse::Failed;
    }

    std::lock_guard<std::mutex> lock(m_lock);
    if(m_mapTidToErrorString.find(key) == m_mapTidToErrorString.end())
    {
        LOG_DEBUG("Setting error to {}", error);
        m_mapTidToErrorString.emplace(key, error);
        return SetErrorResponse::Success;
    }

    LOG_WARN("Multiple error(s) occurred on same thread. GetLastError() will report only first one however.");
    return SetErrorResponse::ErrorAlreadySet;
}
//---------------------------------------------------------------------------------------------------------------------
GetErrorResponse ErrorHandler::GetLastError(/*[out]*/ std::string& error, /*[in]*/ const int key)
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto it = m_mapTidToErrorString.find(key);
    if(it != m_mapTidToErrorString.end())
    {
        error = it->second;
        return GetErrorResponse::Found;
    }

    return GetErrorResponse::NotFound;
}
//---------------------------------------------------------------------------------------------------------------------
bool ErrorHandler::IsErrorSet(/*[in]*/ const int key)
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_mapTidToErrorString.find(key) != m_mapTidToErrorString.end();
}
//---------------------------------------------------------------------------------------------------------------------