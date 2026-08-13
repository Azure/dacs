/// Copyright (c) Microsoft Corporation.
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

#include "IHashingStrategy.h"
#include <bits/stdc++.h>

struct HashTestConfig {
    size_t numKeys = 10000;
    size_t initialServers = 50;
    size_t serversToAdd = 10;
    size_t serversToRemove = 10;
    uint32_t randomSeed = 42;
};

struct BenchmarkResult {
    HashingMetrics initialMetrics;
    HashingMetrics afterAddMetrics;
    HashingMetrics afterRemoveMetrics;
    double addRebalancePercentage;  // Percentage of keys remapped after adding servers
    double removeRebalancePercentage;  // Percentage of keys remapped after removing servers
    size_t keysRebalancedOnAdd; // number of keys remapped after adding servers
    size_t keysRebalancedOnRemove; // number of keys remapped after removing servers
};

class HashingStrategyBenchmark {
public:
    BenchmarkResult RunBenchmark(std::unique_ptr<IHashingStrategy> strategy, const HashTestConfig& config);
    std::vector<std::string> GenerateServerNames(size_t numServers);
    std::vector<std::string> GenerateKeys(size_t numKeys, uint32_t seed);

private:
    std::vector<std::string> SelectRandomServers(const std::vector<std::string>& servers, size_t numRandomServers, uint32_t seed);
    std::pair<double, size_t> CalculateRebalanceFromMappings(
        const std::unordered_map<std::string, std::string>& beforeMapping, // key -> server
        const std::unordered_map<std::string, std::string>& afterMapping); 
};