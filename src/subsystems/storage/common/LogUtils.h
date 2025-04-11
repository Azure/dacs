///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include <atomic>
#include <chrono>
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

#define TIMER_START(timerName) auto timerName##_start = std::chrono::high_resolution_clock::now()
#define TIMER_STOP_AND_LOG(timerName) do {                                                                          \
    auto timerName##_stop = std::chrono::high_resolution_clock::now();                                              \
    auto timerName##_duration = std::chrono::duration_cast<std::chrono::milliseconds>(timerName##_stop - timerName##_start); \
    LOG_INFO("TIMER: {} took {} ms", #timerName, timerName##_duration.count());                                     \
} while (0)

#define DURATION_START(durationName) auto durationName##_start = std::chrono::high_resolution_clock::now();
#define DURATION_MS_GET(durationName) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - durationName##_start).count();

#define STORAGE_ASSERT_NOTHROW(condition, ...) do {                                                                 \
    if (!(condition))                                                                                               \
    {                                                                                                               \
        LOG_CRITICAL("CacheClientError - ASSERTION FAILED: {}", #condition);                                                           \
        LOG_CRITICAL(__VA_ARGS__);                                                                                  \
    }                                                                                                               \
} while (0)

#define STORAGE_ASSERT(condition, ...) do {                                                                         \
    if (!(condition))                                                                                               \
    {                                                                                                               \
        STORAGE_ASSERT_NOTHROW(condition, __VA_ARGS__);                                                             \
        exit(-1);                                                                                                   \
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
#define LOG_DEBUG_REQID_STR(message, reqid_str, args...) \
    LOG_DEBUG(REQID_STR message, reqid_str, ##args)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_INFO_REQID_STR(message, reqid_str, args...) \
    LOG_INFO(REQID_STR message, reqid_str, ##args)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_WARN_REQID_STR(message, reqid_str, args...) \
    LOG_WARN(REQID_STR message, reqid_str, ##args)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_ERROR_REQID_STR(message, reqid_str, args...) \
    LOG_ERROR(REQID_STR message, reqid_str, ##args)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_CRITICAL_REQID_STR(message, reqid_str, args...) \
    LOG_CRITICAL(REQID_STR message, reqid_str, ##args)
//---------------------------------------------------------------------------------------------------------------------
namespace storageutils
{
    bool LogRarelyTime(std::atomic_uint_fast32_t& lastReportTimestampSeconds, uint32_t reportPeriodSeconds);
}
//---------------------------------------------------------------------------------------------------------------------