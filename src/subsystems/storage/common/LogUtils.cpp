///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "LogUtils.h"
#include <time.h>
#include <atomic>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
namespace storageutils
{
    bool LogRarelyTime(std::atomic_uint_fast32_t& lastReportTimestampSeconds, uint32_t reportPeriodSeconds)
    {
        const uint64_t currentTimestamp = time(NULL);
        uint64_t lastReportedTimestamp = lastReportTimestampSeconds;
        if (currentTimestamp > lastReportedTimestamp + reportPeriodSeconds)
        {

            if (lastReportTimestampSeconds.compare_exchange_strong(lastReportedTimestamp, currentTimestamp))
            {
                return true;
            }
        }

        return false;
    }
}
//---------------------------------------------------------------------------------------------------------------------