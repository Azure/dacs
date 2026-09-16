/// Copyright (c) Microsoft Corporation.
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

#pragma once
#include <atomic>
#include <chrono>
#include <iomanip>
//---------------------------------------------------------------------------------------------------------------------
// Some code borrowed from APSDK
#define VAR_NAME_CONCAT(a,b)  a##_##b
#define VAR_NAME_UNIQUE(prefix) VAR_NAME_CONCAT(prefix,__LINE__)
//---------------------------------------------------------------------------------------------------------------------
// Macro to format time_t as readable UTC string for logging
#define LOG_READABLE_TIME_UTC(time_val) std::put_time(std::gmtime(&time_val), "%a %b %d %H:%M:%S %Y UTC")
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
#define LOG_DEBUG_REQID(message, request_object, ...) \
    LOG_DEBUG(REQID_STR message, (request_object).requestid() __VA_OPT__(,) __VA_ARGS__)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_INFO_REQID(message, request_object, ...) \
    LOG_INFO(REQID_STR message, (request_object).requestid() __VA_OPT__(,) __VA_ARGS__)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_WARN_REQID(message, request_object, ...) \
    LOG_WARN(REQID_STR message, (request_object).requestid() __VA_OPT__(,) __VA_ARGS__)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_ERROR_REQID(message, request_object, ...) \
    LOG_ERROR(REQID_STR message, (request_object).requestid() __VA_OPT__(,) __VA_ARGS__)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_CRITICAL_REQID(message, request_object, ...) \
    LOG_CRITICAL(REQID_STR message, (request_object).requestid() __VA_OPT__(,) __VA_ARGS__)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_DEBUG_REQID_STR(message, reqid_str, ...) \
    LOG_DEBUG(REQID_STR message, reqid_str __VA_OPT__(,) __VA_ARGS__)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_INFO_REQID_STR(message, reqid_str, ...) \
    LOG_INFO(REQID_STR message, reqid_str __VA_OPT__(,) __VA_ARGS__)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_WARN_REQID_STR(message, reqid_str, ...) \
    LOG_WARN(REQID_STR message, reqid_str __VA_OPT__(,) __VA_ARGS__)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_ERROR_REQID_STR(message, reqid_str, ...) \
    LOG_ERROR(REQID_STR message, reqid_str __VA_OPT__(,) __VA_ARGS__)
//---------------------------------------------------------------------------------------------------------------------
#define LOG_CRITICAL_REQID_STR(message, reqid_str, ...) \
    LOG_CRITICAL(REQID_STR message, reqid_str __VA_OPT__(,) __VA_ARGS__)
//---------------------------------------------------------------------------------------------------------------------
namespace storageutils
{
    bool LogRarelyTime(std::atomic_uint_fast32_t& lastReportTimestampSeconds, uint32_t reportPeriodSeconds);
}
//---------------------------------------------------------------------------------------------------------------------