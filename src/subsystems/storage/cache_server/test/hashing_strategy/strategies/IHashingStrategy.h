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
#include <bits/stdc++.h>
#include "IHashFunction.h"

struct HashingMetrics {
    std::string hashFunctionName; 
    std::unordered_map<std::string, size_t> serverLoads; // server -> number of keys
    std::unordered_map<std::string, std::string> keyToServerMapping; // key -> server
    size_t emptyServers = 0;
    double maxLoadImbalance = 0.0; // busiest server has this much more load than average
    double standardDeviation = 0.0; // Most servers are within +/- this many keys from the average
};

class IHashingStrategy {
public:
    virtual ~IHashingStrategy() = default;
    virtual std::string GetServer(const std::string& key) const = 0;
    virtual void UpdateServers(const std::vector<std::string>& servers) = 0;
    virtual std::string GetStrategyName() const = 0;
    virtual std::vector<std::string> GetServerList() const = 0;
    virtual std::unique_ptr<IHashingStrategy> CreateInstance(std::unique_ptr<IHashFunction> hashFunc) const = 0;

    // Hash function management
    virtual void SetHashFunction(std::unique_ptr<IHashFunction> hashFunc) = 0;
    virtual std::string GetHashFunctionName() const = 0;
    
    virtual HashingMetrics CalculateMetrics(const std::vector<std::string>& keys) const {
        HashingMetrics metrics;
        metrics.serverLoads.clear();  
        metrics.keyToServerMapping.clear();
        metrics.keyToServerMapping.reserve(keys.size());
        metrics.hashFunctionName = GetHashFunctionName();
        
        // Get server list from derived class
        auto servers = GetServerList();
        
        // Initialize server loads
        for (const auto& server : servers) {
            metrics.serverLoads[server] = 0;
        }

        // Count keys per server
        for (const auto& key : keys) {
            std::string server = GetServer(key);
            if (!server.empty()) {
                metrics.serverLoads[server]++;
                metrics.keyToServerMapping[key] = server; 
            }
        }        
        CalculateDistribution(metrics);
        return metrics;
    }
    
    // Calculate load distribution like maxLoadImbalance and standardDeviation
    virtual void CalculateDistribution(HashingMetrics& metrics) const {
        if (metrics.serverLoads.empty()) return;

        size_t numServers = metrics.serverLoads.size();
        std::vector<size_t> loads;
        loads.reserve(numServers);
        
        size_t totalKeys = 0, maxLoad = 0;
        metrics.emptyServers = 0;
        
        for (const auto& [server, load] : metrics.serverLoads) {
            loads.push_back(load);
            totalKeys += load;
            maxLoad = std::max(maxLoad, load);
            if (load == 0) metrics.emptyServers++;
        }
        
        if (totalKeys == 0) return;

        double avgLoad = static_cast<double>(totalKeys) / numServers;
        metrics.maxLoadImbalance = (maxLoad - avgLoad) / avgLoad;
        
        // Calculate standard deviation
        double variance = 0.0;
        for (size_t load : loads) {
            double diff = load - avgLoad;
            variance += diff * diff;
        }
        variance /= numServers;
        metrics.standardDeviation = std::sqrt(variance);
    }
};