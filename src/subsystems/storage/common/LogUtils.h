///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include <atomic>
//---------------------------------------------------------------------------------------------------------------------
// Some code borrowed from APSDK
#define VAR_NAME_CONCAT(a,b)  a##_##b
#define VAR_NAME_UNIQUE(prefix) VAR_NAME_CONCAT(prefix,__LINE__)

// Log at most once every reportPeriodSeconds seconds
#define LOG_RARELY(reportPeriodSeconds, logLevel, ...) do {                                                         \
    static std::atomic_uint_fast32_t VAR_NAME_UNIQUE(lastReportTimestampSeconds) = 0;                               \
    if (storageutils::LogRarelyTime(VAR_NAME_UNIQUE(lastReportTimestampSeconds), (reportPeriodSeconds)))            \
    {                                                                                                               \
        LOG_##logLevel(__VA_ARGS__);                                                                                \
    }                                                                                                               \
} while (0)
//---------------------------------------------------------------------------------------------------------------------
namespace storageutils
{
    bool LogRarelyTime(std::atomic_uint_fast32_t& lastReportTimestampSeconds, uint32_t reportPeriodSeconds);
}
//---------------------------------------------------------------------------------------------------------------------