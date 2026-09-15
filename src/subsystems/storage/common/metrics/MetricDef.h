///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <string>
#include "Labels.h"
//---------------------------------------------------------------------------------------------------------------------
namespace Dacs::Metrics
{
    // Common descriptor shared by Counter, Gauge, and Histogram metric definitions.
    // autoLabels are static per-metric labels (e.g. {"component", "cache_client"}) that are merged
    // into every observation alongside the global pid/rank labels injected by the store.
    struct MetricDef
    {
        std::string name;
        std::string description;
        Labels      autoLabels;  // static per-metric labels, merged at observation time
    };
}
//---------------------------------------------------------------------------------------------------------------------
