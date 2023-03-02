///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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
        auto t = make_unique<thread>([&reportTime, &numReports, &startTime, this]() {
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
        testThreads.push_back(move(t));
    }
    for (auto &t : testThreads)
    {
        t->join();
    }
    testThreads.clear();

    // Only 2 reports should have been generated
    ASSERT_EQ(numReports, 2);
}
//---------------------------------------------------------------------------------------------------------------------