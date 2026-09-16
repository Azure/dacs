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

//
// The default logging to dacs channel:
// LOG_DEBUG("Testing {}","LOG_DEBUG");
// LOG_INFO("Testing {}","LOG_INFO");
// LOG_ERROR("Testing {}","LOG_ERROR");
// LOG_CRITICAL("Testing {}","LOG_CRITICAL");
// Logs look like :
// DACS_CTR:DEBUG|DeviceProxy|operation_Id:00000000-0000-0000-0000-000000000000|main.cpp:main(9):Testing LOG_DEBUG
// DACS_CTR:INFO|DeviceProxy|operation_Id:00000000-0000-0000-0000-000000000000|main.cpp:main(10):Testing LOG_INFO
// DACS_CTR:ERROR|DeviceProxy|operation_Id:00000000-0000-0000-0000-000000000000|main.cpp:main(11):Testing LOG_ERROR
// DACS_CTR:ERROR|DeviceProxy|operation_Id:00000000-0000-0000-0000-000000000000|main.cpp:main(12):Testing LOG_CRITICAL
//
// The logging to cx channel, these logs should be available to the customer:
// CX_LOG_DEBUG("Testing {}","CX_LOG_DEBUG");
// CX_LOG_INFO("Testing {}","CX_LOG_INFO");
// CX_LOG_ERROR("Testing {}","CX_LOG_ERROR");
// CX_LOG_CRITICAL("Testing {}","CX_LOG_CRITICAL");
// Logs look like :
// CX_RAW:DEBUG|DeviceProxy|operation_Id:00000000-0000-0000-0000-000000000000|main.cpp:main(17):Testing CX_LOG_DEBUG
// CX_RAW:INFO|DeviceProxy|operation_Id:00000000-0000-0000-0000-000000000000|main.cpp:main(18):Testing CX_LOG_INFO
// CX_RAW:ERROR|DeviceProxy|operation_Id:00000000-0000-0000-0000-000000000000|main.cpp:main(19):Testing CX_LOG_ERROR
// CX_RAW:ERROR|DeviceProxy|operation_Id:00000000-0000-0000-0000-000000000000|main.cpp:main(20):Testing CX_LOG_CRITICAL
//
// Compile with -DLOG_LEVEL=0x01 etc to set the right compile time log level
//--------------------------------------------------------------------------------------------------------------------------------------------
#pragma once

#include <iostream>
#include <fstream>
#include <mutex>
#include <assert.h>
#include <string>
#include <chrono>
#include <atomic>
#include <sys/syscall.h>
#include <unistd.h>
#include "ILogger.h"
#include "FileLogger.h"
#include "StandardLogger.h"
#define INVALID_PID -1
#define INVALID_TID -1
#define gettid() syscall(SYS_gettid)
#define GET_TIME std::chrono::duration_cast<std::chrono::nanoseconds>\
(std::chrono::high_resolution_clock::now().time_since_epoch()).count()
//--------------------------------------------------------------------------------------------------------------------------------------------
#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "fmt/core.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"
//-------------------------------------------------------------------------------------------------------------------------------------------
enum class LogLevel
{
    Critical = 0,
    Error = 1,
    Warn = 2,
    Info = 3,
    Debug = 4,
    Trace = 5
};
#ifndef LOG_LEVEL
#define LOG_LEVEL static_cast<int>(LogLevel::Info)
#endif
//-------------------------------------------------------------------------------------------------------------------------------------------
struct AutoOperationIdTLS
{
    AutoOperationIdTLS(std::string&& operationId)   { OperationIdTLS = std::move(operationId); }
    ~AutoOperationIdTLS()                           { OperationIdTLS.clear(); }

    static const std::string& OperationId()         { return OperationIdTLS; }

private:
    static thread_local std::string OperationIdTLS;
};
//-------------------------------------------------------------------------------------------------------------------------------------------
class LoggingApi
{
public:
    static void Init(/*[in]*/ bool bIgnoreRankInFileName = false);
    static void Shutdown();
    static void Flush();

    static void Prepare();
    static void Parent();
    static void Child();
    static void FlushCachedLog();

    // Set the owner information so log messages for a given owner can be correlated. 
    static void SetOwnerId(/*[in]*/ int nOwnerId);

    template<LogLevel LEVEL, typename... _TArgs>
    static void LogMessage(_TArgs&&... Fargs)
    {
        if(!m_bIsInitialized)
        {
            Init();
        }

        static_assert(LEVEL >= LogLevel::Critical && LEVEL <= LogLevel::Trace);

        if(m_nRuntimeLogLevel < static_cast<int>(LEVEL)) return;

        if(LEVEL >= LogLevel::Warn)
        {
            m_upLogger->LogOutput(FormatMsg(std::forward<_TArgs>(Fargs)...));
        }
        else
        {
            m_upLogger->LogError(FormatMsg(std::forward<_TArgs>(Fargs)...));
        }
    }

    LoggingApi() { Init(); }
    ~LoggingApi() { Shutdown(); }

private:
    static std::atomic<bool> m_bIsInitialized;
    static std::atomic<bool> m_bIsInitializing;
    // 1. Proxy client can read its owner from configuration
    // 2. Proxy server will use the owner of proxy client it's attached to.
    // 3. For external processes like CriuDumpHandler etc. it will remain unset.
    static int m_nOwnerId;

    static pid_t m_nCachedPid;
    static thread_local pid_t m_nCachedTidTLS;
    static std::atomic<bool> m_bForkFlag;

    static std::shared_ptr<ILogger> m_upLogger;
    static std::recursive_mutex m_lock;

    static int m_nRuntimeLogLevel;

    template<size_t M, typename... _TArgs>
    static decltype(auto) FormatMsg(const char(&message)[M], _TArgs&&... Fargs)
    {
        // the operation id is always causing log corruption. There is a work item to fix it.
        // meanwhile removing operation id so that we can debug other issues.
        return fmt::format(fmt::runtime(message),
            GET_TIME,
            LoggingApi::Getpid(),
            LoggingApi::Gettid(),
            "",
            m_nOwnerId,
            std::forward<_TArgs>(Fargs)...);
    }

    static pid_t Getpid();
    static pid_t Gettid();
};
//-------------------------------------------------------------------------------------------------------------------------------------------
template<size_t N>
auto parseFileName(const char(&file)[N])
{
    auto pos = strrchr(file, '/');
    return pos ? pos + 1 : file;
}
//-------------------------------------------------------------------------------------------------------------------------------------------
// Ensure number of format specifiers is equal to number of arguments
#if defined(__HIPCC__) || defined(__NVCC__) || defined(__CUDACC__) || defined(AMD_BUILD) || defined(__HIP_PLATFORM_AMD__) || !defined(__cplusplus)
#define STATIC_ASSERT_FORMATTERS(message, ...)                                                                         \
    /** Does not work with device compilers, and is quite limited in AMD **/                                           \
    ;
#else
//-------------------------------------------------------------------------------------------------------------------------------------------
constexpr std::size_t constexpr_strlen(const char* s)
{
    return std::char_traits<char>::length(s);
}
//-------------------------------------------------------------------------------------------------------------------------------------------
constexpr int count_format_specifiers(const char* str)
{
    int count = 0;
    for(std::size_t i = 0; i + 1 < constexpr_strlen(str); i++)
    {
        if(str[i] == '{' && str[i + 1] == '}')
        {
            count++;
            i++;
        }
    }

    return count;
}
//-------------------------------------------------------------------------------------------------------------------------------------------
#define STATIC_ASSERT_FORMATTERS(message, ...)                                                                         \
    static_assert(std::tuple_size<decltype(std::make_tuple(__VA_ARGS__))>::value == count_format_specifiers(message));
#endif
//-------------------------------------------------------------------------------------------------------------------------------------------
#ifdef __FILE_NAME__
#define __FILENAME__ __FILE_NAME__
#else
#define __FILENAME__ parseFileName(__FILE__)
#endif
#ifndef FORMAT_BASE
#define FORMAT_BASE "|DeviceProxy|time_int:{}|pid:{}|tid:{}|"
#endif
#define FORMAT_STR FORMAT_BASE "operation_Id:{}|rank:{}|{}:{}({}): "
//-------------------------------------------------------------------------------------------------------------------------------------------
#define LOG_TRACE(message, ...) \
    STATIC_ASSERT_FORMATTERS(message __VA_OPT__(,) __VA_ARGS__); \
    LoggingApi::LogMessage<LogLevel::Trace>("DACS_CTR:TRACE" FORMAT_STR message, __FILENAME__, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__);
#define CX_LOG_TRACE(message, ...) \
    STATIC_ASSERT_FORMATTERS(message __VA_OPT__(,) __VA_ARGS__); \
    LoggingApi::LogMessage<LogLevel::Trace>("CX_RAW:TRACE" FORMAT_STR message, __FILENAME__, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__);
//-------------------------------------------------------------------------------------------------------------------------------------------
#define LOG_DEBUG(message, ...) \
    STATIC_ASSERT_FORMATTERS(message __VA_OPT__(,) __VA_ARGS__); \
    LoggingApi::LogMessage<LogLevel::Debug>("DACS_CTR:DEBUG" FORMAT_STR message, __FILENAME__, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__);
#define CX_LOG_DEBUG(message, ...) \
    LoggingApi::LogMessage<LogLevel::Debug>("CX_RAW:DEBUG" FORMAT_STR message, __FILENAME__, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__);
//-------------------------------------------------------------------------------------------------------------------------------------------
#define LOG_INFO(message, ...) \
    STATIC_ASSERT_FORMATTERS(message __VA_OPT__(,) __VA_ARGS__); \
    LoggingApi::LogMessage<LogLevel::Info>("DACS_CTR:INFO" FORMAT_STR message, __FILENAME__, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__);
#define CX_LOG_INFO(message, ...) \
    STATIC_ASSERT_FORMATTERS(message __VA_OPT__(,) __VA_ARGS__); \
    LoggingApi::LogMessage<LogLevel::Info>("CX_RAW:INFO" FORMAT_STR message, __FILENAME__, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__);
//-------------------------------------------------------------------------------------------------------------------------------------------
#define LOG_WARN(message, ...) \
    STATIC_ASSERT_FORMATTERS(message __VA_OPT__(,) __VA_ARGS__); \
    LoggingApi::LogMessage<LogLevel::Warn>("DACS_CTR:WARN" FORMAT_STR message, __FILENAME__, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__);
#define CX_LOG_WARN(message, ...) \
    STATIC_ASSERT_FORMATTERS(message __VA_OPT__(,) __VA_ARGS__); \
    LoggingApi::LogMessage<LogLevel::Warn>("CX_RAW:WARN" FORMAT_STR message, __FILENAME__, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__);
//-------------------------------------------------------------------------------------------------------------------------------------------
#define LOG_ERROR(message, ...) \
    STATIC_ASSERT_FORMATTERS(message __VA_OPT__(,) __VA_ARGS__); \
    LoggingApi::LogMessage<LogLevel::Error>("DACS_CTR:ERROR" FORMAT_STR message, __FILENAME__, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__);
#define CX_LOG_ERROR(message, ...) \
    STATIC_ASSERT_FORMATTERS(message __VA_OPT__(,) __VA_ARGS__); \
    LoggingApi::LogMessage<LogLevel::Error>("CX_RAW:ERROR" FORMAT_STR message, __FILENAME__, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__);
//-------------------------------------------------------------------------------------------------------------------------------------------
#define LOG_STACK(message, ...) \
    STATIC_ASSERT_FORMATTERS(message __VA_OPT__(,) __VA_ARGS__); \
    std::cerr << fmt::format("DACS_CTR:ERROR" FORMAT_BASE message, GET_TIME, ::getpid(), ::gettid() __VA_OPT__(,) __VA_ARGS__) <<  std::endl;
//-------------------------------------------------------------------------------------------------------------------------------------------
#define LOG_CRITICAL(message, ...) \
    STATIC_ASSERT_FORMATTERS(message __VA_OPT__(,) __VA_ARGS__); \
    LoggingApi::LogMessage<LogLevel::Critical>("DACS_CTR:CRITICAL" FORMAT_STR message, __FILENAME__, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__);
#define CX_LOG_CRITICAL(message, ...) \
    STATIC_ASSERT_FORMATTERS(message __VA_OPT__(,) __VA_ARGS__); \
    LoggingApi::LogMessage<LogLevel::Critical>("CX_RAW:CRITICAL" FORMAT_STR message, __FILENAME__, __FUNCTION__, __LINE__ __VA_OPT__(,) __VA_ARGS__);
//-------------------------------------------------------------------------------------------------------------------------------------------
