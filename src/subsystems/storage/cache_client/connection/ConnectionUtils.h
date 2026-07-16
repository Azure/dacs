//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <cstdint>
//---------------------------------------------------------------------------------------------------------------------
namespace connectionutils
{
    // Calculate a per-IO timeout by scaling with IO size, clamped to [minMillis, maxMillis].
    // millisPerMB   - additional milliseconds allowed per megabyte of IO.
    // minMillis     - lower bound (returned for very small IOs).
    // maxMillis     - upper bound (returned for very large IOs).
    inline uint64_t CalcIoTimeoutMillis(uint64_t ioSizeInBytes,
                                        uint64_t millisPerMB,
                                        uint64_t minMillis,
                                        uint64_t maxMillis)
    {
        constexpr uint64_t ONE_MB = 1048576;
        uint64_t t = (ioSizeInBytes / ONE_MB) * millisPerMB;
        if (t < minMillis) t = minMillis;
        if (t > maxMillis) t = maxMillis;
        return t;
    }
}
//---------------------------------------------------------------------------------------------------------------------
