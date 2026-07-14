// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

#include "prometheus/counter.h"
#include "prometheus/exposer.h"
#include "prometheus/gauge.h"
#include "prometheus/histogram.h"
#include "prometheus/registry.h"
#include "cache.pb.h"
#include "ResponseCode.h"

using namespace cache;   
class Metrics {
    
    public:

        static void Init();
        static void IncrementActiveConnections();
        static void DecrementActiveConnections();
        static void RegisterRequest(enum Request::TypeCase eRequestType,
                                           enum ResponseCode eResponseCode,
                                           double dLatency,
                                           std::size_t nBytes);
        static void UpdateCacheSize(std::size_t nBytes);
};