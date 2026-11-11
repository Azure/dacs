// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

#include "ILocatorIndex.h"

#include <atomic>
#include <random>
#include <stdexcept>

namespace kvclient
{

uint64_t ILocatorIndex::NewRegistrationGeneration()
{
    // Randomize the starting point to reduce reuse across restarts. Generations are
    // compared only for equality; this is not a persistent, globally ordered epoch.
    static std::atomic<uint64_t> sequence{[] {
        std::random_device random;
        return std::uniform_int_distribution<uint64_t>(0, UINT64_MAX - 1)(random);
    }()};
    uint64_t previous = sequence.load(std::memory_order_relaxed);
    do
    {
        if (previous == UINT64_MAX)
            throw std::overflow_error("Locator registration generation exhausted");
    } while (!sequence.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed));
    return previous + 1;
}

} // namespace kvclient
