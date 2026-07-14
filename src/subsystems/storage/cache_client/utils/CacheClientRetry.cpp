//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------

#include "CacheClientRetry.h"
#include "CacheClientLogger.h"
#include "Retry.h"

#include <thread>
//---------------------------------------------------------------------------------------------------------------------
namespace cacheclientretry
{
const std::unordered_set<CacheClientStatus> RETRYABLE_ERROR_CODES = {
    CacheClientStatus::INTERNAL_ERROR,
};
//---------------------------------------------------------------------------------------------------------------------

bool IsRetryableErrorCode(const CacheClientStatus status)
{
    return RETRYABLE_ERROR_CODES.find(status) != RETRYABLE_ERROR_CODES.end();
}
//---------------------------------------------------------------------------------------------------------------------

bool ValidateRetryConfig(const CacheClientRetryConfig retryConfig)
{
    const uint32_t maxAttempts = retryConfig.nMaxAttempts;
    const uint64_t baseDelayMillis = retryConfig.nBaseDelayMillis;
    const uint64_t maxDelayMillis = retryConfig.nMaxDelayMillis;

    if (maxAttempts < 1 || maxAttempts > MAX_ATTEMPTS_LIMIT)
    {
                LOG_ERROR("Invalid retry config nMaxAttempts {} (valid range: [1, {}])", maxAttempts, MAX_ATTEMPTS_LIMIT);
        return false;
    }

    if (maxAttempts == 1)
    {
        // Retries disabled; delay values are irrelevant.
        return true;
    }

    if (baseDelayMillis == 0 || maxDelayMillis == 0)
    {
        LOG_ERROR(
            "Invalid retry config delays: baseDelayMillis={}, maxDelayMillis={} (must both be > 0)",
            baseDelayMillis,
            maxDelayMillis);
        return false;
    }
    if (baseDelayMillis > maxDelayMillis)
    {
        LOG_ERROR("Invalid retry config delays: baseDelayMillis={} > maxDelayMillis={}", baseDelayMillis, maxDelayMillis);
        return false;
    }

    return true;
}
//---------------------------------------------------------------------------------------------------------------------

std::chrono::milliseconds GetRetryInterval(const uint32_t attemptNumber, const CacheClientRetryConfig retryConfig)
{
    return storageretry::GetRetryIntervalExpJitter(
        attemptNumber,
    std::chrono::milliseconds(retryConfig.nBaseDelayMillis),
    std::chrono::milliseconds(retryConfig.nMaxDelayMillis));
}

CacheClientStatus AttemptWithRetry(
    const std::function<CacheClientStatus()>& attemptFn,
    const std::function<void(const CacheClientRetryLogContext&)>& logFn,
    const CacheClientRetryConfig retryConfig)
{
    if (!ValidateRetryConfig(retryConfig))
    {
        return CacheClientStatus::INVALID_ARGUMENT;
    }

    const uint32_t maxAttempts = retryConfig.nMaxAttempts;

    CacheClientStatus status = CacheClientStatus::INTERNAL_ERROR;

    for (uint32_t  attempt = 1; attempt <= maxAttempts; ++attempt)
    {
        status = attemptFn();

        if (status == CacheClientStatus::SUCCESS)
        {
            return status;
        }

        const bool isRetryableError = IsRetryableErrorCode(status);
        const bool isMaxRetryExhausted = (attempt >= maxAttempts);
        if (!isRetryableError || isMaxRetryExhausted)
        {
            //will not attempt retry
            logFn(CacheClientRetryLogContext{
                static_cast<int>(attempt),
                static_cast<int>(maxAttempts),
                status,
                isRetryableError,
                isMaxRetryExhausted,
                false,
                std::chrono::milliseconds(0),
            });
            return status;
        }

        const auto delay = GetRetryInterval(attempt, retryConfig);
        // attempting retry
        logFn(CacheClientRetryLogContext{
            static_cast<int>(attempt),
            static_cast<int>(maxAttempts),
            status,
            true,
            false,
            true,
            delay,
        });
        std::this_thread::sleep_for(delay);
    }

    return status;
}
//---------------------------------------------------------------------------------------------------------------------
}
