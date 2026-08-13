// Copyright (c) Microsoft Corporation.
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