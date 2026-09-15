//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "MetricsLogger.h"
#include "StorageMetrics.h"
#include "gtest/gtest.h"
//---------------------------------------------------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <poll.h>
#include <signal.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{
namespace
{
    class StorageMetricsTests : public testing::Test
    {
    protected:
        void SetUp() override
        {
            STORAGE_METRIC_SHUTDOWN();
            ASSERT_EQ(unsetenv("RANK"), 0);
            LoggingApi::Init();
            LoggingApi::FlushCachedLog();
            ASSERT_EQ(setenv("STORAGE_METRIC_LOGGING_ENABLED", "true", 1), 0);
            ASSERT_EQ(setenv("STORAGE_METRIC_LOGGING_INTERVAL_MINS", "60", 1), 0);
            ASSERT_EQ(setenv("STORAGE_METRIC_OTEL_ENABLED", "false", 1), 0);
            STORAGE_METRIC_INITIALIZE();
        }

        void TearDown() override
        {
            STORAGE_METRIC_SHUTDOWN();
            ASSERT_EQ(unsetenv("RANK"), 0);
        }

        void CheckCounter(std::string_view output,
                          const Labels&    expectedDefaultLabels,
                          const Labels&    expectedExtraLabels,
                          std::string_view expectedName,
                          int64_t          expectedExtraLabelTotal,
                          int64_t          expectedGlobalTotal)
        {
            const std::string prefix = fmt::format("type=counter name={}", expectedName);
            const auto metricStart = output.find(prefix);
            ASSERT_NE(metricStart, std::string::npos) << output;

            const auto metricEnd = output.find('\n', metricStart);
            const std::string_view metricLine{output.data() + metricStart,
                                              metricEnd == std::string::npos
                                                  ? output.size() - metricStart
                                                  : metricEnd - metricStart};
            EXPECT_NE(metricLine.find("period_start_ms="), std::string::npos) << metricLine;
            EXPECT_NE(metricLine.find("period_end_ms="), std::string::npos) << metricLine;
            EXPECT_NE(metricLine.find(fmt::format(" value={}", expectedGlobalTotal)),
                      std::string::npos) << metricLine;

            for (const auto& [key, value] : expectedDefaultLabels)
            {
                EXPECT_NE(metricLine.find(fmt::format("{}={}", key, value)), std::string::npos)
                    << metricLine;
            }

            if (!expectedExtraLabels.empty())
            {
                std::ostringstream expectedLabelTotalText;
                bool first = true;
                for (const auto& [key, value] : expectedExtraLabels)
                {
                    if (!first) expectedLabelTotalText << ',';
                    expectedLabelTotalText << key << ':' << value;
                    first = false;
                }
                expectedLabelTotalText << '=' << expectedExtraLabelTotal;
                EXPECT_NE(metricLine.find(expectedLabelTotalText.str()), std::string::npos)
                    << metricLine;
            }
        }

        void CheckGauge(std::string_view output,
                        const Labels&   expectedLabels,
                        std::string_view expectedName,
                        double           expectedValue,
                        double           expectedMin,
                        double           expectedMax)
        {
            const std::string prefix = fmt::format("type=gauge name={}", expectedName);
            const auto metricStart = output.find(prefix);
            ASSERT_NE(metricStart, std::string::npos) << output;
            const auto metricEnd = output.find('\n', metricStart);
            const std::string_view metricLine{output.data() + metricStart,
                                              metricEnd == std::string::npos
                                                  ? output.size() - metricStart
                                                  : metricEnd - metricStart};
            for (const auto& [key, value] : expectedLabels)
            {
                EXPECT_NE(metricLine.find(fmt::format("{}={}", key, value)), std::string::npos)
                    << metricLine;
            }
            EXPECT_NE(metricLine.find("period_start_ms="), std::string::npos) << metricLine;
            EXPECT_NE(metricLine.find("period_end_ms="), std::string::npos) << metricLine;
            EXPECT_NE(metricLine.find(fmt::format("value={}", expectedValue)), std::string::npos)
                << metricLine;
            EXPECT_NE(metricLine.find(fmt::format("min={}", expectedMin)), std::string::npos)
                << metricLine;
            EXPECT_NE(metricLine.find(fmt::format("max={}", expectedMax)), std::string::npos)
                << metricLine;
        }

        void CheckHistogram(std::string_view output,
                            const Labels&   expectedLabels,
                            std::string_view expectedName,
                            int64_t          expectedCount,
                            double           expectedMin,
                            double           expectedMax,
                            double           expectedAverage,
                            double           expectedStandardDeviation,
                            double           expectedP50,
                            double           expectedP95,
                            double           expectedP99)
        {
            const std::string prefix = fmt::format("type=histogram name={}", expectedName);
            const auto metricStart = output.find(prefix);
            ASSERT_NE(metricStart, std::string::npos) << output;

            const auto metricEnd = output.find('\n', metricStart);
            const std::string_view metricLine{output.data() + metricStart,
                                              metricEnd == std::string::npos
                                                  ? output.size() - metricStart
                                                  : metricEnd - metricStart};
            for (const auto& [key, value] : expectedLabels)
            {
                EXPECT_NE(metricLine.find(fmt::format("{}={}", key, value)), std::string::npos)
                    << metricLine;
            }

            EXPECT_NE(metricLine.find("period_start_ms="), std::string::npos) << metricLine;
            EXPECT_NE(metricLine.find("period_end_ms="), std::string::npos) << metricLine;
            EXPECT_NE(metricLine.find(fmt::format("count={}", expectedCount)), std::string::npos)
                << metricLine;
            EXPECT_NE(metricLine.find(fmt::format("min={:.2f}", expectedMin)), std::string::npos)
                << metricLine;
            EXPECT_NE(metricLine.find(fmt::format("max={:.2f}", expectedMax)), std::string::npos)
                << metricLine;
            EXPECT_NE(metricLine.find(fmt::format("avg={:.2f}", expectedAverage)), std::string::npos)
                << metricLine;
            EXPECT_NE(metricLine.find(fmt::format("stddev={:.2f}", expectedStandardDeviation)),
                      std::string::npos) << metricLine;
            EXPECT_NE(metricLine.find(fmt::format("p50={:.2f}", expectedP50)), std::string::npos)
                << metricLine;
            EXPECT_NE(metricLine.find(fmt::format("p95={:.2f}", expectedP95)), std::string::npos)
                << metricLine;
            EXPECT_NE(metricLine.find(fmt::format("p99={:.2f}", expectedP99)), std::string::npos)
                << metricLine;
        }

        void CheckHistogramCountAndValue(std::string_view output,
                                         const Labels&   expectedLabels,
                                         std::string_view expectedName,
                                         int64_t          expectedCount,
                                         double           expectedValue)
        {
            CheckHistogram(output,
                           expectedLabels,
                           expectedName,
                           expectedCount,
                           expectedValue,
                           expectedValue,
                           expectedValue,
                           0.0,
                           expectedValue,
                           expectedValue,
                           expectedValue);
        }

    };
}

//---------------------------------------------------------------------------------------------------------------------
TEST_F(StorageMetricsTests, MetricMacrosUpdatePeriodicLoggerStore)
{
    STORAGE_METRIC_REGISTER("requests", "Request count", Labels{{"operation", "read"}});
    STORAGE_METRIC_COUNTER_INCREMENT("requests", 2);
    STORAGE_METRIC_COUNTER_INCREMENT("requests", 3);

    STORAGE_METRIC_SET_GAUGE("queue_depth", 12.0);
    STORAGE_METRIC_SET_GAUGE("queue_depth", 7.0);

    for (int i = 1; i <= 20; ++i)
    {
        STORAGE_METRIC_OBSERVE_HISTOGRAM("latency_ms", 1.0 * i);
    }

    testing::internal::CaptureStdout();
    STORAGE_METRIC_SHUTDOWN();
    const std::string output = testing::internal::GetCapturedStdout();
    const std::string pid = std::to_string(::getpid());

    CheckCounter(output, Labels{{"pid", pid}, {"operation", "read"}}, {}, "requests", 0, 5);
    CheckGauge(output, Labels{{"pid", pid}}, "queue_depth", 7.0, 7.0, 12.0);
    CheckHistogram(
        output,
        Labels{{"pid", pid}},
        "latency_ms",
        20, /* expected count */
        1.0, /* expected min */
        20.0, /* expected max */
        10.5, /* expected avg */
        5.766, /* expected standard deviation */
        10.0, /* expected p50 */
        19.0, /* expected p95 */
        20.0  /* expected p99 */);
}

//---------------------------------------------------------------------------------------------------------------------
TEST_F(StorageMetricsTests, HistogramSupportsMoreThanMaximumSamples)
{
    constexpr int64_t observationCount = 3000;
    constexpr double observationValue = 2.0;
    for (int64_t index = 0; index < observationCount; ++index)
    {
        STORAGE_METRIC_OBSERVE_HISTOGRAM("large_histogram", observationValue);
    }

    testing::internal::CaptureStdout();
    STORAGE_METRIC_SHUTDOWN();
    const std::string output = testing::internal::GetCapturedStdout();

    CheckHistogram(output,
                   Labels{{"pid", std::to_string(::getpid())}},
                   "large_histogram",
                   observationCount,
                   observationValue,
                   observationValue,
                   observationValue,
                   0.0,
                   observationValue,
                   observationValue,
                   observationValue);
}

//---------------------------------------------------------------------------------------------------------------------
TEST_F(StorageMetricsTests, TimerMacrosRecordLatencyAndIoSizeBucket)
{
    constexpr std::array<uint64_t, 8> ioSizes{
        10 * 1024,
        40 * 1024,
        80 * 1024,
        400 * 1024,
        4 * 1024 * 1024,
        16 * 1024 * 1024,
        40 * 1024 * 1024,
        80 * 1024 * 1024};
    for (const auto ioSize : ioSizes)
    {
        STORAGE_METRIC_TIMER_START(timer_macro_latency_ms);
        STORAGE_METRIC_TIMER_END(timer_macro_latency_ms, ioSize);
    }

    testing::internal::CaptureStdout();
    STORAGE_METRIC_FLUSH();
    const std::string output = testing::internal::GetCapturedStdout();

    const std::string pid = std::to_string(::getpid());
    // Verify overall latency histogram
    CheckHistogramCountAndValue(output,
                                Labels{{"pid", pid}},
                                "timer_macro_latency_ms",
                                static_cast<int64_t>(ioSizes.size()),
                                0.0);
    // Verify the 40KB bucket.
    CheckHistogramCountAndValue(
        output, Labels{{"pid", pid}}, "timer_macro_latency_ms_40KB", 2, 0.0); // 10KB and 40KB
    // Verify the 400KB bucket,
    CheckHistogramCountAndValue(
        output, Labels{{"pid", pid}}, "timer_macro_latency_ms_400KB", 2, 0.0); // 80KB and 400KB
    // Verify the 4MB bucket.
    CheckHistogramCountAndValue(
        output, Labels{{"pid", pid}}, "timer_macro_latency_ms_4MB", 1, 0.0); // 4MB
    // Verify the 40MB bucket.
    CheckHistogramCountAndValue(
        output, Labels{{"pid", pid}}, "timer_macro_latency_ms_40MB", 2, 0.0); // 16MB and 40MB
    // Verify the >40MB bucket.
    CheckHistogramCountAndValue(
        output, Labels{{"pid", pid}}, "timer_macro_latency_ms_huge", 1, 0.0); // 80MB
}

//---------------------------------------------------------------------------------------------------------------------
TEST_F(StorageMetricsTests, CounterTracksTotalsForEachLabelSet)
{
    STORAGE_METRIC_COUNTER_INCREMENT("dns_error", 2, Labels{{"hostname", "cacheserver-12"}});
    STORAGE_METRIC_COUNTER_INCREMENT("dns_error", 3, Labels{{"hostname", "cacheserver-5"}});
    STORAGE_METRIC_COUNTER_INCREMENT("dns_error", 5, Labels{{"hostname", "cacheserver-12"}});

    testing::internal::CaptureStdout();
    STORAGE_METRIC_FLUSH();
    const std::string output = testing::internal::GetCapturedStdout();
    const Labels defaultLabels{{"pid", std::to_string(::getpid())}};
    CheckCounter(output,
                 defaultLabels,
                 Labels{{"hostname", "cacheserver-12"}},
                 "dns_error",
                 7,
                 10);
    CheckCounter(output,
                 defaultLabels,
                 Labels{{"hostname", "cacheserver-5"}},
                 "dns_error",
                 3,
                 10);
}

//---------------------------------------------------------------------------------------------------------------------
TEST_F(StorageMetricsTests, FlushResetsAllMetricTypes)
{
    const Labels expectedLabels{{"pid", std::to_string(::getpid())}};

    STORAGE_METRIC_COUNTER_INCREMENT("period_counter", 3);
    STORAGE_METRIC_SET_GAUGE("period_gauge", 4.0);
    STORAGE_METRIC_OBSERVE_HISTOGRAM("period_histogram", 5.0);

    testing::internal::CaptureStdout();
    STORAGE_METRIC_FLUSH();
    const std::string firstPeriod = testing::internal::GetCapturedStdout();
    CheckCounter(firstPeriod, expectedLabels, {}, "period_counter", 0, 3);
    CheckGauge(firstPeriod, expectedLabels, "period_gauge", 4.0, 4.0, 4.0);
    CheckHistogram(firstPeriod, expectedLabels, "period_histogram", 1,
                   5.0, 5.0, 5.0, 0.0, 5.0, 5.0, 5.0);

    testing::internal::CaptureStdout();
    STORAGE_METRIC_FLUSH();
    const std::string emptyPeriod = testing::internal::GetCapturedStdout();
    EXPECT_EQ(emptyPeriod.find("type=counter"), std::string::npos) << emptyPeriod;
    EXPECT_EQ(emptyPeriod.find("type=gauge"), std::string::npos) << emptyPeriod;
    EXPECT_EQ(emptyPeriod.find("type=histogram"), std::string::npos) << emptyPeriod;

    STORAGE_METRIC_COUNTER_INCREMENT("period_counter", 7);
    STORAGE_METRIC_SET_GAUGE("period_gauge", 8.0);
    STORAGE_METRIC_OBSERVE_HISTOGRAM("period_histogram", 9.0);

    testing::internal::CaptureStdout();
    STORAGE_METRIC_FLUSH();
    const std::string secondPeriod = testing::internal::GetCapturedStdout();
    CheckCounter(secondPeriod, expectedLabels, {}, "period_counter", 0, 7);
    CheckGauge(secondPeriod, expectedLabels, "period_gauge", 8.0, 8.0, 8.0);
    CheckHistogram(secondPeriod, expectedLabels, "period_histogram", 1,
                   9.0, 9.0, 9.0, 0.0, 9.0, 9.0, 9.0);
}

//---------------------------------------------------------------------------------------------------------------------
TEST_F(StorageMetricsTests, RankEnvironmentVariableIsAddedToMetricLabels)
{
    STORAGE_METRIC_SHUTDOWN();
    ASSERT_EQ(setenv("RANK", "42", 1), 0);
    STORAGE_METRIC_INITIALIZE();

    STORAGE_METRIC_SET_GAUGE("ranked_requests", 3);

    testing::internal::CaptureStdout();
    STORAGE_METRIC_SHUTDOWN();
    const std::string output = testing::internal::GetCapturedStdout();

    CheckGauge(output,
               Labels{{"pid", std::to_string(::getpid())}, {"rank", "42"}},
               "ranked_requests",
               3,
               3,
               3);
}

//---------------------------------------------------------------------------------------------------------------------
TEST_F(StorageMetricsTests, ForkHandlingTest)
{
    const Labels parentLabels{{"pid", std::to_string(::getpid())}};
    int pipeFds[2];
    ASSERT_EQ(::pipe(pipeFds), 0);

    const pid_t childPid = ::fork();
    if (childPid < 0)
    {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        FAIL() << "fork failed";
    }

    if (childPid == 0)
    {
        // In the child process after fork, redirect stdout to the pipe for capturing metrics output.
        ::close(pipeFds[0]);
        if (::dup2(pipeFds[1], STDOUT_FILENO) < 0)
        {
            ::close(pipeFds[1]);
            ::_exit(1);
        }
        ::close(pipeFds[1]);

        // Emit a metric in the child process after fork.
        STORAGE_METRIC_INITIALIZE();
        STORAGE_METRIC_COUNTER_INCREMENT("after_fork", 2);
        STORAGE_METRIC_SHUTDOWN();
        std::fflush(stdout);
        // Avoid running inherited gtest and process-exit cleanup in the child.
        ::_exit(0);
    }

    ::close(pipeFds[1]);
    struct pollfd childOutput{
        .fd = pipeFds[0],
        .events = POLLIN,
        .revents = 0,
    };
    // Bound the wait so a fork-related metrics deadlock fails instead of hanging the suite.
    const int pollResult = ::poll(&childOutput, 1, 10000);
    if (pollResult <= 0)
    {
        ::kill(childPid, SIGKILL);
        ::waitpid(childPid, nullptr, 0);
        ::close(pipeFds[0]);
        FAIL() << "child metrics operation did not complete before timeout";
    }

    // Transfer child logs to the parent because gtest assertions must run only in the parent.
    std::string childOutputText;
    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = ::read(pipeFds[0], buffer, sizeof(buffer))) > 0)
    {
        childOutputText.append(buffer, static_cast<size_t>(bytesRead));
    }
    ::close(pipeFds[0]);

    int childStatus = 0;
    ASSERT_EQ(::waitpid(childPid, &childStatus, 0), childPid);
    ASSERT_TRUE(WIFEXITED(childStatus));
    ASSERT_EQ(WEXITSTATUS(childStatus), 0);
    CheckCounter(childOutputText,
                 Labels{{"pid", std::to_string(childPid)}},
                 {},
                 "after_fork",
                 0,
                 2);

    // Validate metrics work independently in parent after the fork.
    testing::internal::CaptureStdout();
    STORAGE_METRIC_INITIALIZE();
    STORAGE_METRIC_COUNTER_INCREMENT("after_fork", 3);
    STORAGE_METRIC_FLUSH();
    const std::string parentOutput = testing::internal::GetCapturedStdout();
    CheckCounter(parentOutput,
                 parentLabels,
                 {},
                 "after_fork",
                 0,
                 3);
}

//---------------------------------------------------------------------------------------------------------------------
TEST(StorageMetricsNoInitializationTests, MetricMacrosAreNoOpBeforeInitialization)
{
    STORAGE_METRIC_SHUTDOWN();
    STORAGE_METRIC_REGISTER("ignored", "Ignored metric");
    STORAGE_METRIC_COUNTER_INCREMENT("ignored");
    STORAGE_METRIC_SET_GAUGE("ignored", 1.0);
    STORAGE_METRIC_OBSERVE_HISTOGRAM("ignored", 1.0);
}
//---------------------------------------------------------------------------------------------------------------------
} // namespace Dacs::Metrics
//---------------------------------------------------------------------------------------------------------------------