///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include <atomic>
//---------------------------------------------------------------------------------------------------------------------
// Some code borrowed from APSDK
#define VAR_NAME_CONCAT(a,b)  a##_##b
#define VAR_NAME_UNIQUE(prefix) VAR_NAME_CONCAT(prefix,__LINE__)
//---------------------------------------------------------------------------------------------------------------------
// Log at most once every reportPeriodSeconds seconds
#define LOG_RARELY(reportPeriodSeconds, logLevel, ...) do {                                                         \
    static std::atomic_uint_fast32_t VAR_NAME_UNIQUE(lastReportTimestampSeconds) = 0;                               \
    if (storageutils::LogRarelyTime(VAR_NAME_UNIQUE(lastReportTimestampSeconds), (reportPeriodSeconds)))            \
    {                                                                                                               \
        LOG_##logLevel(__VA_ARGS__);                                                                                \
    }                                                                                                               \
} while (0)
//---------------------------------------------------------------------------------------------------------------------
// Macros to log with request ID. The first argument after the message must be the request ID string.
#define REQID_STR "reqid:{}| "
//---------------------------------------------------------------------------------------------------------------------
#define LOG_DEBUG_REQID(message, request_object, args...) \
    LOG_DEBUG(REQID_STR message, (request_object).requestid(), ##args)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_INFO_REQID(message, request_object, args...) \
    LOG_INFO(REQID_STR message, (request_object).requestid(), ##args)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_WARN_REQID(message, request_object, args...) \
    LOG_WARN(REQID_STR message, (request_object).requestid(), ##args)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_ERROR_REQID(message, request_object, args...) \
    LOG_ERROR(REQID_STR message, (request_object).requestid(), ##args)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_CRITICAL_REQID(message, request_object, args...) \
    LOG_CRITICAL(REQID_STR message, (request_object).requestid(), ##args)
//---------------------------------------------------------------------------------------------------------------------
namespace storageutils
{
    bool LogRarelyTime(std::atomic_uint_fast32_t& lastReportTimestampSeconds, uint32_t reportPeriodSeconds);
}
//---------------------------------------------------------------------------------------------------------------------