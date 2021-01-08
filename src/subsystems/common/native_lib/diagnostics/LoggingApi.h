//--------------------------------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//--------------------------------------------------------------------------------------------------------------------------------------------
//
// The default logging to dacs channel:
// LOG_DEBUG("Testing {}","LOG_DEBUG");
// LOG_INFO("Testing {}","LOG_INFO");
// LOG_ERROR("Testing {}","LOG_ERROR");
// LOG_CRITICAL("Testing {}","LOG_CRITICAL");
// Logs look like :
// DACS:DEBUG|main.cpp|main(9):Testing LOG_DEBUG
// DACS:INFO|main.cpp|main(10):Testing LOG_INFO
// DACS:ERROR|main.cpp|main(11):Testing LOG_ERROR
// DACS:ERROR|main.cpp|main(12):Testing LOG_CRITICAL
//
// The logging to cx channel, these logs should be available to the customer:
// CX_LOG_DEBUG("Testing {}","CX_LOG_DEBUG");
// CX_LOG_INFO("Testing {}","CX_LOG_INFO");
// CX_LOG_ERROR("Testing {}","CX_LOG_ERROR");
// CX_LOG_CRITICAL("Testing {}","CX_LOG_CRITICAL");
// Logs look like :
// DACS_CTR:DEBUG|main.cpp|main(17):Testing CX_LOG_DEBUG
// DACS_CTR:INFO|main.cpp|main(18):Testing CX_LOG_INFO
// DACS_CTR:ERROR|main.cpp|main(19):Testing CX_LOG_ERROR
// DACS_CTR:ERROR|main.cpp|main(20):Testing CX_LOG_CRITICAL
//
// The logging to ctr channel, these logs are for container infra event:
// CTR_LOG_DEBUG("Testing {}","CTR_LOG_DEBUG");
// CTR_LOG_INFO("Testing {}","CTR_LOG_INFO");
// CTR_LOG_ERROR("Testing {}","CTR_LOG_ERROR");
// CTR_LOG_CRITICAL("Testing {}","CTR_LOG_CRITICAL");
// Logs look like :
// DACS_CTR:DEBUG|main.cpp|main(13):Testing CTR_LOG_DEBUG
// DACS_CTR:INFO|main.cpp|main(14):Testing CTR_LOG_INFO
// DACS_CTR:ERROR|main.cpp|main(15):Testing CTR_LOG_ERROR
// DACS_CTR:ERROR|main.cpp|main(16):Testing CTR_LOG_CRITICAL
//
// Compile with -DLOG_LEVEL=0x01 etc to set the right compile time log level
//--------------------------------------------------------------------------------------------------------------------------------------------
#pragma once

#include <iostream>
#include <assert.h>
#include <string>
//--------------------------------------------------------------------------------------------------------------------------------------------
#define FMT_HEADER_ONLY
#include "spdlog/fmt/bundled/format.h"
#include "spdlog/fmt/bundled/core.h"
#include "spdlog/fmt/bundled/ostream.h"
//-------------------------------------------------------------------------------------------------------------------------------------------
#define LOGGER_NAME "dacs_logger"
#define BACKTRACE_LOG_DEPTH 64
//-------------------------------------------------------------------------------------------------------------------------------------------
namespace LoggingApi
{
    void init(/*[in]*/ bool bSyncMode = false);
};
//-------------------------------------------------------------------------------------------------------------------------------------------
#define CRITICAL_LOG 0x00
#define ERROR_LEVEL 0x01
#define WARN_LEVEL 0x02
#define INFO_LEVEL 0x03
#define DEBUG_LEVEL 0x04
#define TRACE_LEVEL 0x05
#ifndef LOG_LEVEL
#define LOG_LEVEL DEBUG_LEVEL
#endif
//-------------------------------------------------------------------------------------------------------------------------------------------
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
//-------------------------------------------------------------------------------------------------------------------------------------------
#if LOG_LEVEL >= TRACE_LEVEL
#define LOG_TRACE(message, args...) \
    std::cout << fmt::format("DACS:TRACE|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define CTR_LOG_TRACE(message, args...) \
    std::cout << fmt::format("DACS_CTR:TRACE|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define CX_LOG_TRACE(message, args...) \
    std::cout << fmt::format("CX_RAW:TRACE|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#else
#define LOG_TRACE(msg, args...)
#define CTR_LOG_TRACE(msg, args...)
#define CX_LOG_TRACE(msg, args...)
#endif
//-------------------------------------------------------------------------------------------------------------------------------------------
#if LOG_LEVEL >= DEBUG_LEVEL
#define LOG_DEBUG(message, args...) \
    std::cout << fmt::format("DACS:DEBUG|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define CTR_LOG_DEBUG(message, args...) \
    std::cout << fmt::format("DACS_CTR:DEBUG|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define CX_LOG_DEBUG(message, args...) \
    std::cout << fmt::format("CX_RAW:DEBUG|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#else
#define LOG_DEBUG(message, args...)
#define CTR_LOG_DEBUG(message, args...)
#define CX_LOG_DEBUG(message, args...)
#endif
//-------------------------------------------------------------------------------------------------------------------------------------------
#if LOG_LEVEL >= INFO_LEVEL
#define LOG_INFO(message, args...) \
    std::cout << fmt::format("DACS:INFO|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define CTR_LOG_INFO(message, args...) \
    std::cout << fmt::format("DACS_CTR:INFO|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define CX_LOG_INFO(message, args...) \
    std::cout << fmt::format("CX_RAW:INFO|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#else
#define LOG_INFO(message, args...)
#define CTR_LOG_INFO(message, args...)
#define CX_LOG_INFO(message, args...)
#endif
//-------------------------------------------------------------------------------------------------------------------------------------------
#if LOG_LEVEL >= WARN_LEVEL
#define LOG_WARN(message, args...) \
    std::cout << fmt::format("DACS:WARN|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define CTR_LOG_WARN(message, args...) \
    std::cout << fmt::format("DACS_CTR:WARN|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define CX_LOG_WARN(message, args...) \
    std::cout << fmt::format("CX_RAW:WARN|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#else
#define LOG_WARN(message, args...)
#define CTR_LOG_WARN(message, args...)
#define CX_LOG_WARN(message, args...)
#endif
//-------------------------------------------------------------------------------------------------------------------------------------------
#if LOG_LEVEL >= ERROR_LEVEL
#define LOG_ERROR(message, args...) \
    std::cout << fmt::format("DACS:ERROR|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define CTR_LOG_ERROR(message, args...) \
    std::cout << fmt::format("DACS_CTR:ERROR|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define CX_LOG_ERROR(message, args...) \
    std::cout << fmt::format("CX_RAW:ERROR|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define LOG_STACK(message, args...) \
    std::cout << fmt::format("DACS:ERROR|" message, ##args) <<  std::endl;
#else
#define LOG_ERROR(message, args...)
#define CTR_LOG_ERROR(message, args...)
#define CX_LOG_ERROR(message, args...)
#define LOG_STACK(message, args...)
#endif
//-------------------------------------------------------------------------------------------------------------------------------------------
#if LOG_LEVEL >= CRITICAL_LOG
#define LOG_CRITICAL(message, args...) \
    std::cout << fmt::format("DACS:CRITICAL|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define CTR_LOG_CRITICAL(message, args...) \
    std::cout << fmt::format("DACS_CTR:CRITICAL|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#define CX_LOG_CRITICAL(message, args...) \
    std::cout << fmt::format("CX_RAW:CRITICAL|{}|{}({}):" message, __FILENAME__, __FUNCTION__, __LINE__, ##args) <<  std::endl;
#else
#define LOG_CRITICAL(message, args...)
#define CTR_LOG_CRITICAL(message, args...)
#define CX_LOG_CRITICAL(message, args...)
#endif
//-------------------------------------------------------------------------------------------------------------------------------------------