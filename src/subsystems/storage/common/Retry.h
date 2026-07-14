//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <chrono>
#include <cstdint>
//---------------------------------------------------------------------------------------------------------------------
namespace storageretry
{
std::chrono::milliseconds GetRetryIntervalExpJitter(
    uint32_t retryNumber,
    std::chrono::milliseconds baseDelay,
    std::chrono::milliseconds maxDelay);

std::chrono::seconds GetRetryIntervalExpJitter(
    uint32_t retryNumber,
    std::chrono::seconds baseDelay,
    std::chrono::seconds maxDelay);

}
//---------------------------------------------------------------------------------------------------------------------
