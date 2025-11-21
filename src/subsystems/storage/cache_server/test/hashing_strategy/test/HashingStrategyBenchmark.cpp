///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "HashingStrategyBenchmark.h"
#include <bits/stdc++.h>

using namespace std;

BenchmarkResult HashingStrategyBenchmark::RunBenchmark(std::unique_ptr<IHashingStrategy> strategy, const HashTestConfig& config) {
    BenchmarkResult result;
    auto keys = GenerateKeys(config.numKeys, config.randomSeed);
    auto initialServers = GenerateServerNames(config.initialServers);
    
    // Baseline
    strategy->UpdateServers(initialServers);
    result.initialMetrics = strategy->CalculateMetrics(keys);

    // SCALE UP (Add servers)
    auto currentServers = strategy->GetServerList();
    for (size_t i = 0; i < config.serversToAdd; ++i) {
        currentServers.push_back("server_" + std::to_string(config.initialServers + i));
    }
    strategy->UpdateServers(currentServers);
    result.afterAddMetrics = strategy->CalculateMetrics(keys);
    std::tie(result.addRebalancePercentage, result.keysRebalancedOnAdd) = CalculateRebalanceFromMappings(result.initialMetrics.keyToServerMapping, result.afterAddMetrics.keyToServerMapping);
    
    // SCALE DOWN (Remove random servers)
    currentServers = strategy->GetServerList();
    std::vector<std::string> serversToRemove = SelectRandomServers(currentServers, config.serversToRemove, config.randomSeed + 1000);
    for (const auto& serverToRemove : serversToRemove) {
        currentServers.erase(
            std::remove(currentServers.begin(), currentServers.end(), serverToRemove),
            currentServers.end()
        );
    }
    strategy->UpdateServers(currentServers);
    result.afterRemoveMetrics = strategy->CalculateMetrics(keys);
    std::tie(result.removeRebalancePercentage, result.keysRebalancedOnRemove) = CalculateRebalanceFromMappings(result.afterAddMetrics.keyToServerMapping, result.afterRemoveMetrics.keyToServerMapping);
    return result;
}

std::pair<double, size_t> HashingStrategyBenchmark::CalculateRebalanceFromMappings(const std::unordered_map<std::string, std::string>& beforeMapping, const std::unordered_map<std::string, std::string>& afterMapping) {
    size_t changedKeys = 0;
    for (const auto& [key, beforeServer] : beforeMapping) {
        auto afterIt = afterMapping.find(key);
        if (afterIt != afterMapping.end() && beforeServer != afterIt->second) {
            changedKeys++;
        }
    }
    return {(static_cast<double>(changedKeys) / beforeMapping.size()) * 100.0, changedKeys};
}

std::vector<std::string> HashingStrategyBenchmark::SelectRandomServers(const std::vector<std::string>& servers, size_t numRandomServers, uint32_t seed) {
    if (numRandomServers >= servers.size()) {
        return servers; // Return all if count exceeds available servers
    }
    std::vector<std::string> selectedServers;
    std::vector<std::string> availableServers = servers; // Copy for shuffling
    
    std::mt19937 rng(seed);
    std::shuffle(availableServers.begin(), availableServers.end(), rng);

    // Take the first 'numRandomServers' servers from the shuffled list
    selectedServers.reserve(numRandomServers);
    for (size_t i = 0; i < numRandomServers; ++i) {
        selectedServers.push_back(availableServers[i]);
    }
    
    return selectedServers;
}

std::vector<std::string> HashingStrategyBenchmark::GenerateKeys(size_t numKeys, uint32_t seed) {
    std::vector<std::string> keys;
    keys.reserve(numKeys);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint64_t> dist;
    for (size_t i = 0; i < numKeys; ++i) {
        keys.push_back("key_" + std::to_string(dist(rng)));
    }
    return keys;
}

std::vector<std::string> HashingStrategyBenchmark::GenerateServerNames(size_t numServers) {
    std::vector<std::string> servers;
    servers.reserve(numServers);
    for (size_t i = 0; i < numServers; ++i) {
        servers.push_back("server_" + std::to_string(i));
    }
    return servers;
}