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

#include "HashingStrategyBenchmark.h"
#include "StaticHashingStrategy.h"
#include "ConsistentHashingStrategy.h"
#include "RendezvousHashingStrategy.h"
#include "HashFunctions.h"
#include "OutputFormatters.h"
#include <bits/stdc++.h>

using namespace std;

//config
HashTestConfig GetDefaultConfig() {
    HashTestConfig config;
    config.numKeys = 100000;
    config.initialServers = 100;
    config.serversToAdd = 20;
    config.serversToRemove = 40;
    std::random_device rd;
    config.randomSeed = rd();
    return config;
}

// Hash function name -> hash function creator
unordered_map<string, function<unique_ptr<IHashFunction>()>> hashCreators = {
    {"fnv",     []() { return make_unique<FNV1aHash>(); }},
    {"std",     []() { return make_unique<StdHash>(); }},
    {"murmur",  []() { return make_unique<Murmur128Hash>(); }},
    {"xxhash",  []() { return make_unique<XXHash3Hash>(); }},
    {"sha256",  []() { return make_unique<SHA256Hash>(); }}
};

// Strategy name -> Strategy  creator
unordered_map<string, function<unique_ptr<IHashingStrategy>(unique_ptr<IHashFunction>)>> strategyCreators = {
    {"static",     [](auto h) { return make_unique<StaticHashingStrategy>(std::move(h)); }},
    {"consistent", [](auto h) { return make_unique<ConsistentHashingStrategy>(991, std::move(h)); }}, //vNodes = 991
    {"rendezvous", [](auto h) { return make_unique<RendezvousHashingStrategy>(std::move(h)); }}
};

// factories
std::vector<std::unique_ptr<IHashFunction>> CreateAllHashFunctions() {
    std::vector<std::unique_ptr<IHashFunction>> funcs;
    for (const auto& [_, function] : hashCreators) {
            funcs.push_back(function());
    }
    return funcs;
}

std::unique_ptr<IHashFunction> CreateHashFunction(const string& hashName) {
    auto it = hashCreators.find(hashName);
    return (it != hashCreators.end()) ? it->second() : nullptr;
}

std::vector<std::unique_ptr<IHashingStrategy>> CreateAllStrategies(const IHashFunction& hashFunc) {
    std::vector<std::unique_ptr<IHashingStrategy>> strategies;
    for (const auto& [_, creator] : strategyCreators) {
        strategies.push_back(creator(hashFunc.Clone()));
    }
    return strategies;
}

unique_ptr<IHashingStrategy> CreateStrategy(const string& name, unique_ptr<IHashFunction> hashFunc) {
    auto it = strategyCreators.find(name);
    return (it != strategyCreators.end()) ? it->second(std::move(hashFunc)) : nullptr;
}

//perf profiling
double MeasureGetServerTiming(IHashingStrategy& strategy, const vector<string>& keys, 
                             const vector<string>& servers) {
    strategy.UpdateServers(servers);
    
    auto start = chrono::high_resolution_clock::now();
    for (const auto& key : keys) {
        strategy.GetServer(key);
    }
    auto end = chrono::high_resolution_clock::now();
    
    return chrono::duration<double, nano>(end - start).count() / keys.size();
}

void PrintPerformanceResults(const IHashFunction& hashFunc, const HashTestConfig& config) {
    HashingStrategyBenchmark benchmark;
    auto strategies = CreateAllStrategies(hashFunc);
    auto keys = benchmark.GenerateKeys(10000, config.randomSeed); 
    auto servers = benchmark.GenerateServerNames(config.initialServers);
    
    cout << "\nPerformance Results (" << hashFunc.GetName() << "):\n";
    cout << string(50, '-') << "\n";
    
    for (auto& strategy : strategies) {
        double avgTime = MeasureGetServerTiming(*strategy, keys, servers);
        cout << left << setw(20) << strategy->GetStrategyName() 
             << fixed << setprecision(1) << avgTime << " ns/call\n";
    }
}

// test execution

void RunSingleBenchmark(unique_ptr<IHashingStrategy> strategy, const HashTestConfig& config) {
    cout << strategy->GetStrategyName() << " + " << strategy->GetHashFunctionName() << "\n";
    cout << "Config: " << config.numKeys << " keys, " << config.initialServers << " servers\n\n";

    HashingStrategyBenchmark benchmark;
    auto result = benchmark.RunBenchmark(std::move(strategy), config);

    PrintLoadSummary(result.initialMetrics, "Initial");
    PrintLoadSummary(result.afterAddMetrics, "After Adding " + to_string(config.serversToAdd));
    PrintLoadSummary(result.afterRemoveMetrics, "After Removing " + to_string(config.serversToRemove));
    PrintSummaryMetrics(result);
}

void RunStrategyComparison(const IHashFunction& hashFunc, const HashTestConfig& config) {
    PrintHeader("STRATEGY COMPARISON: " + hashFunc.GetName());
    
    auto strategies = CreateAllStrategies(hashFunc);
    for (auto& strategy : strategies) {
        cout << "\n" << string(60, '-') << "\n";
        RunSingleBenchmark(std::move(strategy), config);
    }
    
    PrintPerformanceResults(hashFunc, config);
}

void RunFullMatrix(const HashTestConfig& config) {
    PrintHeader("FULL MATRIX: ALL STRATEGIES × ALL HASH FUNCTIONS");
    
    auto hashFunctions = CreateAllHashFunctions();
    for (auto& hashFunc : hashFunctions) {
        RunStrategyComparison(*hashFunc, config);
    }
    
    cout << "\nMatrix Complete: " << hashFunctions.size() 
         << " hash functions × 3 strategies = " << (hashFunctions.size() * 3) 
         << " combinations tested\n";
}

void RunQuickComparison(const HashTestConfig& config) {
    PrintHeader("QUICK PERFORMANCE SUMMARY");    
    PrintTableHeader();
    
    HashingStrategyBenchmark benchmark;
    auto hashFunctions = CreateAllHashFunctions();
    
    for (auto& hashFunc : hashFunctions) {
        auto strategies = CreateAllStrategies(*hashFunc);
        auto keys = benchmark.GenerateKeys(10000, config.randomSeed);
        auto servers = benchmark.GenerateServerNames(config.initialServers);
        
        for (auto& strategy : strategies) {
            // Run benchmark
            auto result = benchmark.RunBenchmark(strategy->CreateInstance(hashFunc->Clone()), config);
            
            // Measure timing
            auto timingStrategy = strategy->CreateInstance(hashFunc->Clone());
            double avgTime = MeasureGetServerTiming(*timingStrategy, keys, servers);
            
            
            // Print row
            PrintTableRow(strategy->GetStrategyName(), hashFunc->GetName(), result, avgTime);
        }
    }
}

int main(int argc, char** argv) {
    auto config = GetDefaultConfig();
    
    if (argc == 1) {
        cout << "Running default test with FNV-1a hash...\n";
        cout << "Use '" << argv[0] << " --help' for more options\n\n";
        RunStrategyComparison(FNV1aHash(), config);
        return 0;
    }
    
    string command = argv[1];
    
    if (command == "--help" || command == "-h") {
        PrintUsage(argv[0]);
        return 0;
    }
    
    if (command == "matrix") {
        RunFullMatrix(config);
    }
    else if (command == "quick") {
        RunQuickComparison(config);
    }
    else if (command == "hash" && argc >= 3) {
        auto hashFunc = CreateHashFunction(argv[2]);
        if (!hashFunc) {
            cout << "Error: Unknown hash function '" << argv[2] << "'\n";
            cout << "Available: fnv, std, murmur, xxhash, sha256\n";
            return 1;
        }
        RunStrategyComparison(*hashFunc, config);
    }
    else if (command == "single" && argc >= 4) {
        auto hashFunc = CreateHashFunction(argv[3]);
        auto strategy = CreateStrategy(argv[2], std::move(hashFunc));
        
        if (!strategy) {
            cout << "Error: Invalid strategy '" << argv[2] << "' or hash '" << argv[3] << "'\n";
            PrintUsage(argv[0]);
            return 1;
        }
        
        RunSingleBenchmark(std::move(strategy), config);
    }
    else {
        cout << "Error: Invalid command\n";
        PrintUsage(argv[0]);
        return 1;
    }
    
    return 0;
}