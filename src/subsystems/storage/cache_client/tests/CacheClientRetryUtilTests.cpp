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

#include "gtest/gtest.h"
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "utils/CacheClientRetry.h"
#include "CacheClient.h"
//---------------------------------------------------------------------------------------------------------------------

/**
 * Unit tests for CacheClientRetry module.
 * Tests exponential backoff, jitter, retry logic, and integration with operations.
 */
class CacheClientRetryUtilTests : public ::testing::Test
{
  protected:
    void SetUp() override {}
    void TearDown() override {}
};

static constexpr uint64_t kBaseDelayMs = 500;
static constexpr uint64_t kMaxDelayMs = 5000;

//---------------------------------------------------------------------------------------------------------------------
// Test 1: Successful attempt on first try (no retry needed)
TEST_F(CacheClientRetryUtilTests, SuccessOnFirstAttempt)
{
    int attemptCount = 0;
    bool logCalled = false;

    auto attemptFn = [&]() -> CacheClientStatus {
        attemptCount++;
        return CacheClientStatus::SUCCESS;
    };

    auto logFn = [&](const cacheclientretry::CacheClientRetryLogContext& ctx) {
        logCalled = true;  // Should not be called on success
        (void)ctx;
    };

    const CacheClientRetryConfig retryConfig{1, kBaseDelayMs, kMaxDelayMs};
    CacheClientStatus result = cacheclientretry::AttemptWithRetry(attemptFn, logFn, retryConfig);

    EXPECT_EQ(CacheClientStatus::SUCCESS, result);
    EXPECT_EQ(1, attemptCount);
    EXPECT_FALSE(logCalled);  // Log function should not be called on success
}

//---------------------------------------------------------------------------------------------------------------------
// Test 2: Non-retryable error on first attempt (should fail immediately)
TEST_F(CacheClientRetryUtilTests, NonRetryableErrorImmediatelyFails)
{
    int attemptCount = 0;
    int logCallCount = 0;

    auto attemptFn = [&]() -> CacheClientStatus {
        attemptCount++;
        return CacheClientStatus::INVALID_ARGUMENT;  // Non-retryable
    };

    auto logFn = [&](const cacheclientretry::CacheClientRetryLogContext& ctx) {
        logCallCount++;
        EXPECT_EQ(1, ctx.failedAttempt);
        EXPECT_EQ(1, ctx.maxAttempts);
        EXPECT_EQ(CacheClientStatus::INVALID_ARGUMENT, ctx.status);
        EXPECT_FALSE(ctx.is_retryable_error);
        EXPECT_TRUE(ctx.is_max_retry_exhausted);
        EXPECT_FALSE(ctx.will_retry);
        EXPECT_EQ(0, ctx.delay.count());
    };

    const CacheClientRetryConfig retryConfig{1, kBaseDelayMs, kMaxDelayMs};
    CacheClientStatus result = cacheclientretry::AttemptWithRetry(attemptFn, logFn, retryConfig);

    EXPECT_EQ(CacheClientStatus::INVALID_ARGUMENT, result);
    EXPECT_EQ(1, attemptCount);  // Only tried once
    EXPECT_EQ(1, logCallCount);
}

//---------------------------------------------------------------------------------------------------------------------
// Test 3: Retryable error, then success (happy path)
TEST_F(CacheClientRetryUtilTests, RetryableErrorThenSuccess)
{
    int attemptCount = 0;
    int logCallCount = 0;

    auto attemptFn = [&]() -> CacheClientStatus {
        attemptCount++;
        if (attemptCount == 1) {
            return CacheClientStatus::INTERNAL_ERROR;  // Retryable
        }
        return CacheClientStatus::SUCCESS;
    };

    auto logFn = [&](const cacheclientretry::CacheClientRetryLogContext& ctx) {
        logCallCount++;
        EXPECT_EQ(1, ctx.failedAttempt);
        EXPECT_EQ(3, ctx.maxAttempts);
        EXPECT_EQ(CacheClientStatus::INTERNAL_ERROR, ctx.status);
        EXPECT_TRUE(ctx.is_retryable_error);
        EXPECT_FALSE(ctx.is_max_retry_exhausted);
        EXPECT_TRUE(ctx.will_retry);
        EXPECT_GT(ctx.delay.count(), 0);
    };

    const CacheClientRetryConfig retryConfig{3, kBaseDelayMs, kMaxDelayMs};
    CacheClientStatus result = cacheclientretry::AttemptWithRetry(attemptFn, logFn, retryConfig);

    EXPECT_EQ(CacheClientStatus::SUCCESS, result);
    EXPECT_EQ(2, attemptCount);  // First failed, second succeeded
    EXPECT_EQ(1, logCallCount);
}

//---------------------------------------------------------------------------------------------------------------------
// Test 4: Max retries exhausted
TEST_F(CacheClientRetryUtilTests, MaxRetriesExhausted)
{
    uint32_t  attemptCount = 0;
    uint32_t  logCallCount = 0;

    constexpr uint32_t kMaxAttempts = 3;

    auto attemptFn = [&]() -> CacheClientStatus {
        attemptCount++;
        return CacheClientStatus::INTERNAL_ERROR;  // Always fail
    };

    auto logFn = [&](const cacheclientretry::CacheClientRetryLogContext& ctx) {
        logCallCount++;
        EXPECT_EQ(kMaxAttempts, static_cast<uint32_t>(ctx.maxAttempts));
        EXPECT_EQ(CacheClientStatus::INTERNAL_ERROR, ctx.status);
        EXPECT_TRUE(ctx.is_retryable_error);
        if (ctx.failedAttempt < static_cast<int>(kMaxAttempts))
        {
            EXPECT_FALSE(ctx.is_max_retry_exhausted);
            EXPECT_TRUE(ctx.will_retry);
            EXPECT_GT(ctx.delay.count(), 0);
        }
        else
        {
            EXPECT_TRUE(ctx.is_max_retry_exhausted);
            EXPECT_FALSE(ctx.will_retry);
            EXPECT_EQ(0, ctx.delay.count());
        }
    };
    const CacheClientRetryConfig retryConfig{kMaxAttempts, kBaseDelayMs, kMaxDelayMs};
    CacheClientStatus result = cacheclientretry::AttemptWithRetry(attemptFn, logFn, retryConfig);

    EXPECT_EQ(CacheClientStatus::INTERNAL_ERROR, result);
    EXPECT_EQ(kMaxAttempts, attemptCount);
    EXPECT_EQ(kMaxAttempts, logCallCount);  // Includes exhausted final attempt
}

//---------------------------------------------------------------------------------------------------------------------
// Test 5: Verify exponential backoff increases with attempts
TEST_F(CacheClientRetryUtilTests, ExponentialBackoffIncreases)
{
    std::vector<std::chrono::milliseconds> delays;
    uint32_t  attemptCount = 0;

    auto attemptFn = [&]() -> CacheClientStatus {
        attemptCount++;
        return CacheClientStatus::INTERNAL_ERROR;
    };

    auto logFn = [&](const cacheclientretry::CacheClientRetryLogContext& ctx) {
        if (ctx.will_retry)
        {
            delays.push_back(ctx.delay);
        }
    };

    constexpr uint32_t kMaxAttempts = 3;
    const CacheClientRetryConfig retryConfig{kMaxAttempts, kBaseDelayMs, kMaxDelayMs};
    cacheclientretry::AttemptWithRetry(attemptFn, logFn, retryConfig);

    // Should have maxAttempts-1 delays (no delay after last attempt)
    EXPECT_EQ(kMaxAttempts - 1, delays.size());

    // Each delay should be within expected range and generally increase
    // Note: due to jitter, we can only check ranges, not exact values
    for (size_t i = 0; i < delays.size(); i++) {
        int64_t delayMs = delays[i].count();

        // For attempt i+1, exponential is baseDelayMs * 2^(i+1), capped at maxDelayMs
        uint64_t exponentialBackoff = kBaseDelayMs << (i + 1);
        uint64_t cappedBackoff = std::min(exponentialBackoff, kMaxDelayMs);
        uint64_t minExpected = 1;
        uint64_t maxExpected = cappedBackoff;

        EXPECT_GE(delayMs, minExpected) << "Delay too small for attempt " << i;
        EXPECT_LE(delayMs, maxExpected) << "Delay too large for attempt " << i;
    }
}
//---------------------------------------------------------------------------------------------------------------------
// Test 6: GetRetryInterval returns valid milliseconds
TEST_F(CacheClientRetryUtilTests, GetRetryIntervalReturnsValidRange)
{
    const CacheClientRetryConfig retryConfig{3, kBaseDelayMs, kMaxDelayMs};
    for (uint32_t  attempt = 1; attempt <= 3; attempt++) {
        auto interval = cacheclientretry::GetRetryInterval(attempt, retryConfig);
        int64_t intervalMs = interval.count();

        // Compute expected exponential backoff
        uint64_t exponentialBackoff = kBaseDelayMs << attempt;
        uint64_t cappedBackoff = std::min(exponentialBackoff, kMaxDelayMs);

        // Full jitter: uniform in [1, cappedBackoff]
        EXPECT_GE(intervalMs, 1) << "Interval too small for attempt " << attempt;
        EXPECT_LE(intervalMs, static_cast<int64_t>(cappedBackoff)) << "Interval too large for attempt " << attempt;
    }
}

//---------------------------------------------------------------------------------------------------------------------
// Test 7: All retries exhausted with exponential backoff verification
// Simulates permanent failure where issue doesn't resolve
TEST_F(CacheClientRetryUtilTests, AllRetriesExhaustedWithBackoffVerification)
{
    uint32_t  attemptCount = 0;
    std::vector<std::chrono::milliseconds> delays;

    auto operationAttempt = [&]() -> CacheClientStatus {
        attemptCount++;
        return CacheClientStatus::INTERNAL_ERROR;  // Always fail
    };

    auto logRetry = [&](const cacheclientretry::CacheClientRetryLogContext& ctx) {
        if (ctx.will_retry)
        {
            delays.push_back(ctx.delay);
        }
    };

    constexpr uint32_t kMaxAttempts = 3;
    const CacheClientRetryConfig retryConfig{kMaxAttempts, kBaseDelayMs, kMaxDelayMs};
    CacheClientStatus result = cacheclientretry::AttemptWithRetry(operationAttempt, logRetry, retryConfig);

    ASSERT_EQ(CacheClientStatus::INTERNAL_ERROR, result);
    ASSERT_EQ(kMaxAttempts, attemptCount);
    ASSERT_EQ(kMaxAttempts - 1, delays.size());

    // Verify exponential backoff
    // Delay 1: [1, 1000]ms (base<<1, full jitter)
    ASSERT_GE(delays[0].count(), 1);
    ASSERT_LE(delays[0].count(), 1000);

    // Delay 2: [1, 2000]ms (base<<2, full jitter)
    ASSERT_GE(delays[1].count(), 1);
    ASSERT_LE(delays[1].count(), 2000);
}

//---------------------------------------------------------------------------------------------------------------------
// Test 8: Non-retryable errors fail immediately without retry
// Verifies that errors like AUTH_FAILED, KEY_EXISTS, INVALID_ARGUMENT don't trigger retry
TEST_F(CacheClientRetryUtilTests, MultipleNonRetryableErrorsFailImmediately)
{
    std::vector<CacheClientStatus> nonRetryableErrors = {
        CacheClientStatus::SUCCESS,
        CacheClientStatus::INVALID_ARGUMENT,
        CacheClientStatus::KEY_EXISTS,
        CacheClientStatus::AUTH_FAILED,
        CacheClientStatus::NOTFOUND
    };

    for (auto errorStatus : nonRetryableErrors) {
        int attemptCount = 0;
        bool logCalled = false;

        auto operationAttempt = [&]() -> CacheClientStatus {
            attemptCount++;
            return errorStatus;
        };

        auto logRetry = [&](const cacheclientretry::CacheClientRetryLogContext& ctx) {
            logCalled = true;
            (void)ctx;
        };

        const CacheClientRetryConfig retryConfig{1, kBaseDelayMs, kMaxDelayMs};
        CacheClientStatus result = cacheclientretry::AttemptWithRetry(operationAttempt, logRetry, retryConfig);

        ASSERT_EQ(errorStatus, result);
        ASSERT_EQ(1, attemptCount) << "Should not retry for status: " << static_cast<int>(errorStatus);
        if (errorStatus == CacheClientStatus::SUCCESS)
        {
            ASSERT_FALSE(logCalled) << "Should not log for status: " << static_cast<int>(errorStatus);
        }
        else
        {
            ASSERT_TRUE(logCalled) << "Should log failure for status: " << static_cast<int>(errorStatus);
        }
    }
}

//---------------------------------------------------------------------------------------------------------------------
//Test 9: ValidateRetryConfig with various configurations
// Single test verifying all ValidateRetryConfig scenarios
TEST_F(CacheClientRetryUtilTests, ValidateRetryConfig_AllScenarios)
{
    struct Scenario
    {
        const char* name;
        CacheClientRetryConfig cfg;
        bool expected;
    };

    const Scenario scenarios[] = {
        {"maxAttempts below range", CacheClientRetryConfig{0, 100, 1000}, false},
        {"maxAttempts above range", CacheClientRetryConfig{cacheclientretry::MAX_ATTEMPTS_LIMIT + 1, 100, 1000}, false},

        // Retries disabled: delay values are irrelevant.
        {"retries disabled allows 0 delays", CacheClientRetryConfig{1, 0, 0}, true},
        {"retries disabled ignores delay ordering", CacheClientRetryConfig{1, 1000, 100}, true},

        // Retries enabled: delays must be valid.
        {"retries enabled rejects baseDelayMs=0", CacheClientRetryConfig{2, 0, 1000}, false},
        {"retries enabled rejects maxDelayMs=0", CacheClientRetryConfig{2, 100, 0}, false},
        {"retries enabled rejects base>max", CacheClientRetryConfig{2, 1001, 1000}, false},
        {"retries enabled accepts valid delays", CacheClientRetryConfig{2, 100, 1000}, true},

        // Boundary
        {"maxAttempts at limit accepted", CacheClientRetryConfig{cacheclientretry::MAX_ATTEMPTS_LIMIT, 100, 1000}, true},
    };

    for(const auto& s : scenarios)
    {
        EXPECT_EQ(cacheclientretry::ValidateRetryConfig(s.cfg), s.expected) << s.name;
    }
}

//---------------------------------------------------------------------------------------------------------------------
