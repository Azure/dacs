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

#pragma once
#include "HashingStrategyBenchmark.h"
#include <bits/stdc++.h>

using namespace std;

void PrintHeader(const string& title, char separator = '=', int width = 80) {
    cout << "\n" << string(width, separator) << "\n";
    cout << title << "\n";
    cout << string(width, separator) << "\n";
}

void PrintSummaryMetrics(const BenchmarkResult& result) {
    cout << "\nSummary Metrics:\n";
    cout << "  Rebalancing: " << fixed << setprecision(1) 
         << result.addRebalancePercentage << "% (add) / " 
         << result.removeRebalancePercentage << "% (remove)\n";
    cout << "  Load Balance: " << fixed << setprecision(1) 
         << (result.initialMetrics.maxLoadImbalance * 100) << "% (initial) → " 
         << (result.afterAddMetrics.maxLoadImbalance * 100) << "% (after add) → "
         << (result.afterRemoveMetrics.maxLoadImbalance * 100) << "% (after remove)\n";
}

void PrintLoadSummary(const HashingMetrics& metrics, const string& phase) {
    auto loads = vector<size_t>();
    for (const auto& [server, load] : metrics.serverLoads) {
        loads.push_back(load);
    }
    
    auto [minIt, maxIt] = minmax_element(loads.begin(), loads.end());
    double avg = accumulate(loads.begin(), loads.end(), 0.0) / loads.size();
    
    cout << phase << " Load Distribution:\n";
    cout << "  Range: " << *minIt << " - " << *maxIt << " keys\n";
    cout << "  Average: " << fixed << setprecision(1) << avg << " keys\n";
    cout << "  Imbalance: " << fixed << setprecision(1) 
         << (metrics.maxLoadImbalance * 100) << "%\n\n";
}

void PrintTableHeader() {
    cout << left << setw(18) << "Strategy" << setw(20) << "Hash Function"
         << setw(15) << "AddRebal%" << setw(15) << "RemoveRebal%" 
         << setw(15) << "InitialImbal%" << setw(15) << "AddImbal%" 
         << setw(15) << "RemoveImbal%" << setw(15) << "ns/call" << "\n";
    cout << string(110, '-') << "\n";
}

void PrintTableRow(const std::string& strategy, const std::string& hashFunc, BenchmarkResult result, double avgTime) {
    cout << left << setw(18) << strategy
         << setw(20) << hashFunc
         << setw(15) << fixed << setprecision(1) << result.addRebalancePercentage
         << setw(15) << fixed << setprecision(1) << result.removeRebalancePercentage
         << setw(15) << fixed << setprecision(1) << (result.initialMetrics.maxLoadImbalance * 100)
         << setw(15) << fixed << setprecision(1) << (result.afterAddMetrics.maxLoadImbalance * 100)
         << setw(15) << fixed << setprecision(1) << (result.afterRemoveMetrics.maxLoadImbalance * 100)
         << setw(15) << fixed << setprecision(1) << avgTime << "\n";
}

void PrintUsage(const char* programName) {
    cout << "Usage: " << programName << " [command] [options]\n\n";
    cout << "Commands:\n";
    cout << "  matrix                    Full matrix test (all combinations)\n";
    cout << "  quick                     Summary table with performance\n";
    cout << "  hash <name>               Test all strategies with one hash function\n";
    cout << "  single <strategy> <hash>  Test one strategy+hash combination\n\n";
    cout << "Hash Functions: fnv, std, murmur, xxhash, sha256\n";
    cout << "Strategies: static, consistent, rendezvous\n";
}



