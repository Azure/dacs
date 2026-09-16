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

// This file implements the StorageInterceptUtilsTest.cpp
//---------------------------------------------------------------------------------------------------------------------
#include "LogUtils.h"
#include "gtest/gtest.h"
#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include <time.h>
//---------------------------------------------------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------------------------------------------------
TEST(StorageUtilsTest, TestLogRarely)
{
    time_t startTime = time(NULL);
    std::atomic_uint_fast32_t reportTime = 0;

    // Check LogRarely count
    int numReports = 0;
    while (difftime(time(NULL), startTime) < 7)
    {
        if (storageutils::LogRarelyTime(reportTime, 5))
        {
            numReports++;
        }

        // Avoid CPU spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Only 2 reports should have been generated
    ASSERT_EQ(numReports, 2);
}
//---------------------------------------------------------------------------------------------------------------------
TEST(StorageUtilsTest, TestLogRarelyParallel)
{
    time_t startTime = time(NULL);
    std::atomic_uint_fast32_t reportTime = 0;

    // Check LogRarely count in parallel
    std::atomic_uint_fast32_t numReports = 0;
    vector<unique_ptr<thread>> testThreads;
    for (int i = 0; i < 20; ++i)
    {
        auto t = make_unique<thread>([&reportTime, &numReports, &startTime]() {
            while (difftime(time(NULL), startTime) < 9)
            {
                if (storageutils::LogRarelyTime(reportTime, 5))
                {
                    numReports++;
                }

                // Avoid CPU spinning
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        testThreads.push_back(std::move(t));
    }
    for (auto &t : testThreads)
    {
        t->join();
    }
    testThreads.clear();

    // Only 2 reports should have been generated
    ASSERT_EQ(numReports.load(), uint_fast32_t{2});
}
//---------------------------------------------------------------------------------------------------------------------