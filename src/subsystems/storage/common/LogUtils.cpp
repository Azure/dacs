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

#include "LogUtils.h"
#include <time.h>
#include <atomic>
#include <limits>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
namespace storageutils
{
    bool LogRarelyTime(std::atomic_uint_fast32_t& lastReportTimestampSeconds, uint32_t reportPeriodSeconds)
    {
        const time_t currentTime = time(nullptr);
        if (currentTime < 0 || static_cast<uintmax_t>(currentTime) > std::numeric_limits<uint_fast32_t>::max())
        {
            return false;
        }

        const auto currentTimestamp = static_cast<uint_fast32_t>(currentTime);
        uint_fast32_t lastReportedTimestamp = lastReportTimestampSeconds;
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