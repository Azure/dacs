// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

#include "ILocatorIndex.h"

#include <atomic>
#include <random>

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
    return NewRegistrationGeneration(sequence);
}

uint64_t ILocatorIndex::NewRegistrationGeneration(std::atomic<uint64_t>& sequence)
{
    uint64_t generation;
    do
    {
        generation = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    } while (generation == 0);
    return generation;
}

} // namespace kvclient
