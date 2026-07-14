///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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