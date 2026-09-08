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
        static void SetListenBacklogMax(std::size_t backlog);
        static void IncrementAcceptQueueOverflows(uint64_t delta);
        static void IncrementListenDrops(uint64_t delta);
        static void IncrementSynQueueFullDrops(uint64_t delta);
        static void IncrementSynCookiesSent(uint64_t delta);
        static void IncrementPassiveOpens(uint64_t delta);
        static void UpdateOpenFdCount(std::size_t count);
        static void IncrementAcceptError(int errnum);
};