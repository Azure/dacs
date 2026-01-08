//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "Retry.h"

#include <algorithm>
#include <random>
//---------------------------------------------------------------------------------------------------------------------
namespace storageretry
{
//---------------------------------------------------------------------------------------------------------------------
template <typename Duration>
static Duration GetRetryIntervalExpJitterInternal(uint32_t retryNumber, Duration baseDelay, Duration maxDelay)
{
    const uint32_t maxShift = 20; // safety check to prevent overflow
    const uint32_t shift = std::min<uint32_t>(retryNumber, maxShift);

    const uint64_t base = static_cast<uint64_t>(baseDelay.count());
    const uint64_t cap = static_cast<uint64_t>(maxDelay.count());

    const uint64_t exponentialBackoff = base << shift;
    const uint64_t cappedBackoff = std::min<uint64_t>(cap, exponentialBackoff);

    thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist(1, cappedBackoff);
    return Duration(dist(rng));
}
//---------------------------------------------------------------------------------------------------------------------
std::chrono::milliseconds GetRetryIntervalExpJitter(
    uint32_t retryNumber,
    std::chrono::milliseconds baseDelay,
    std::chrono::milliseconds maxDelay)
{
    return GetRetryIntervalExpJitterInternal(retryNumber, baseDelay, maxDelay);
}
//---------------------------------------------------------------------------------------------------------------------
std::chrono::seconds GetRetryIntervalExpJitter(
    uint32_t retryNumber,
    std::chrono::seconds baseDelay,
    std::chrono::seconds maxDelay)
{
    return GetRetryIntervalExpJitterInternal(retryNumber, baseDelay, maxDelay);
}
//---------------------------------------------------------------------------------------------------------------------
}
