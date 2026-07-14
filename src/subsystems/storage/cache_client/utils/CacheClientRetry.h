//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "CacheClient.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_set>
//---------------------------------------------------------------------------------------------------------------------
namespace cacheclientretry
{
static constexpr uint32_t MAX_ATTEMPTS_LIMIT = 100;

// Retryable error codes 
extern const std::unordered_set<CacheClientStatus> RETRYABLE_ERROR_CODES;

bool IsRetryableErrorCode(CacheClientStatus status);
// Exponential backoff with jitter.
std::chrono::milliseconds GetRetryInterval(
    uint32_t attemptNumber,
    CacheClientRetryConfig retryConfig);

// Retry attempt with configurable parameters.
bool ValidateRetryConfig(
    CacheClientRetryConfig retryConfig);

struct CacheClientRetryLogContext
{
    int failedAttempt;
    int maxAttempts;
    CacheClientStatus status;
    bool is_retryable_error;
    bool is_max_retry_exhausted;
    bool will_retry;
    std::chrono::milliseconds delay;
};

CacheClientStatus AttemptWithRetry(
    const std::function<CacheClientStatus()>& attemptFn,
    const std::function<void(const CacheClientRetryLogContext&)>& logFn,
    CacheClientRetryConfig retryConfig);
}
//---------------------------------------------------------------------------------------------------------------------