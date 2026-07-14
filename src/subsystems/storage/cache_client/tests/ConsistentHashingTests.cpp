//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "gtest/gtest.h"
#include <chrono>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <random>
#include <fstream>

#include "ConsistentHashingTests.h"
#include "CacheUtils.h"
#include "CommonLogger.h"
#include "LogUtils.h"

using namespace std;
using namespace cacheutils;
//---------------------------------------------------------------------------------------------------------------------
// Helper to initialize client 
void ConsistentHashingTests::InitializeClient(CacheClientImpl& client)
{
    uint32_t nPort = 9065; // Default test port
    if (const char* envPort = getenv("CACHE_SERVER_PORT")) {
        nPort = atoi(envPort);
    }
    
    const char* arrServers[] = {
        "cacheserver-0.azureml.svc.cluster.local",
        "cacheserver-1.azureml.svc.cluster.local", 
        "cacheserver-2.azureml.svc.cluster.local"
    };

    const CacheClientRetryConfig retryConfig{1, 0, 0};
    client.Initialize(arrServers,
                      3,
                      nPort,
                      false,
                      false,
                      false,
                      nullptr,
                      0,
                      false,
                      "",
                      "",
                      false,
                      true,
                      false,
                      5,
                      "",
                      true,
                      retryConfig);
}
//---------------------------------------------------------------------------------------------------------------------
vector<string> ConsistentHashingTests::GenerateKeys(int count, const string& prefix)
{
    vector<string> keys;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint64_t> dist;
    for (int i = 0; i < count; i++) {
        keys.push_back(prefix + std::to_string(dist(rng)));
    }
    return keys;
}
//---------------------------------------------------------------------------------------------------------------------
// Consistent Hashing Functional Tests
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConsistentHashingTests, ServerSelectionConsistency) {
    CacheClientImpl client;
    InitializeClient(client);
    ASSERT_TRUE(client.IsInitialized());
    
    auto keys = GenerateKeys(100);
    
    // Verify same key always maps to same server
    for (const auto& key : keys) {
        auto server1 = client.m_spConnectionManager->GetCacheServerHostname(key);
        auto server2 = client.m_spConnectionManager->GetCacheServerHostname(key);
        
        ASSERT_EQ(server1, server2) << "Inconsistent mapping for key: " << key;
        ASSERT_FALSE(server1.empty()) << "Empty server for key: " << key;
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConsistentHashingTests, ServerDistribution) {
    CacheClientImpl client;
    InitializeClient(client);
    ASSERT_TRUE(client.IsInitialized());
    
    auto servers = client.m_spConnectionManager->GetAllCacheServerHostnames();
    ASSERT_GT(servers.size(), 0);
    
    auto keys = GenerateKeys(300);
    unordered_map<string, int> serverCounts;
    
    // Count key distribution
    for (const auto& key : keys) {
        auto server = client.m_spConnectionManager->GetCacheServerHostname(key);
        ASSERT_FALSE(server.empty());
        serverCounts[server]++;
    }
    
    uint32_t expectedPerServer = keys.size() / servers.size();

    // Each server should receive at least some keys
    for (const auto& server : servers) {
        ASSERT_GT(serverCounts[server], expectedPerServer * 0.5) << "Server " << server << " received no keys";
    }
    
    // Should only use configured servers
    ASSERT_EQ(serverCounts.size(), servers.size());
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConsistentHashingTests, ScaleUpDown) {
    CacheClientImpl client;
    InitializeClient(client);
    ASSERT_TRUE(client.IsInitialized());
    
    auto keys = GenerateKeys(100);
    
    // Phase 1: 3 servers
    vector<string> servers3 = {
        "cacheserver-0.azureml.svc.cluster.local",
        "cacheserver-1.azureml.svc.cluster.local",
        "cacheserver-2.azureml.svc.cluster.local"
    };
    client.m_spConnectionManager->UpdateCacheServers(servers3);
    
    auto currentServers = client.m_spConnectionManager->GetAllCacheServerHostnames();
    ASSERT_EQ(currentServers.size(), 3);
    
    // Verify all keys map to valid servers
    for (const auto& key : keys) {
        auto server = client.m_spConnectionManager->GetCacheServerHostname(key);
        ASSERT_TRUE(find(servers3.begin(), servers3.end(), server) != servers3.end());
    }
    
    // Phase 2: Scale up to 5 servers
    vector<string> servers5 = servers3;
    servers5.push_back("cacheserver-3.azureml.svc.cluster.local");
    servers5.push_back("cacheserver-4.azureml.svc.cluster.local");

    client.m_spConnectionManager->UpdateCacheServers(servers5);
    currentServers = client.m_spConnectionManager->GetAllCacheServerHostnames();
    ASSERT_EQ(currentServers.size(), 5);
    
    // Verify keys still map to valid servers (now including new ones)
    unordered_map<string, int> counts5;
    for (const auto& key : keys) {
        auto server = client.m_spConnectionManager->GetCacheServerHostname(key);
        ASSERT_TRUE(find(servers5.begin(), servers5.end(), server) != servers5.end());
        counts5[server]++;
    }
    
    // New servers should be receiving keys
    ASSERT_GT(counts5["cacheserver-3.azureml.svc.cluster.local"], 0);
    ASSERT_GT(counts5["cacheserver-4.azureml.svc.cluster.local"], 0);

    // Phase 3: Scale down to 2 servers
    vector<string> servers2 = {
        "cacheserver-0.azureml.svc.cluster.local",
        "cacheserver-1.azureml.svc.cluster.local"
    };
    
    client.m_spConnectionManager->UpdateCacheServers(servers2);
    currentServers = client.m_spConnectionManager->GetAllCacheServerHostnames();
    ASSERT_EQ(currentServers.size(), 2);
    
    // Verify keys only map to remaining servers
    for (const auto& key : keys) {
        auto server = client.m_spConnectionManager->GetCacheServerHostname(key);
        ASSERT_TRUE(find(servers2.begin(), servers2.end(), server) != servers2.end());
    }
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConsistentHashingTests, ThreadSafety) {
    CacheClientImpl client;
    InitializeClient(client);
    ASSERT_TRUE(client.IsInitialized());
    
    const int numThreads = 4;
    const int keysPerThread = 50;
    atomic<bool> failed(false);
    vector<thread> threads;
    
    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([&, t]() {
            auto keys = GenerateKeys(keysPerThread, "thread" + to_string(t) + "_");
            
            for (const auto& key : keys) {
                auto server1 = client.m_spConnectionManager->GetCacheServerHostname(key);
                auto server2 = client.m_spConnectionManager->GetCacheServerHostname(key);
                
                if (server1 != server2 || server1.empty()) {
                    failed = true;
                    return;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    ASSERT_FALSE(failed);
}
//---------------------------------------------------------------------------------------------------------------------
// Performance Validation Tests
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConsistentHashingTests, Performance) {
    CacheClientImpl client;
    InitializeClient(client);
    ASSERT_TRUE(client.IsInitialized());
    
    const int numKeys = 10000;
    auto keys = GenerateKeys(numKeys);
    
    auto start = chrono::high_resolution_clock::now();
    
    for (const auto& key : keys) {
        auto server = client.m_spConnectionManager->GetCacheServerHostname(key);
        ASSERT_FALSE(server.empty());
    }
    
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end - start);
    
    // Assert reasonable performance: < 50μs per key selection
    double avgTimePerKey = static_cast<double>(duration.count()) / numKeys;
    ASSERT_LT(avgTimePerKey, 50.0) << "Server selection too slow: " << avgTimePerKey << "μs/key";
}
//---------------------------------------------------------------------------------------------------------------------
TEST_F(ConsistentHashingTests, DistributionAnalysis) {
    CacheClientImpl client;
    InitializeClient(client);
    ASSERT_TRUE(client.IsInitialized());
    
    const int numKeys = 100000;
    const int initialServers = 100;
    
    // Helper to print distribution statistics
    auto printDistribution = [](const std::string& phase, 
                                 const std::unordered_map<std::string, int>& distribution,
                                 int totalKeys) {
        std::vector<int> counts;
        for (const auto& [server, count] : distribution) {
            counts.push_back(count);
        }
        std::sort(counts.begin(), counts.end());
        
        int min = counts.front();
        int max = counts.back();
        int median = counts[counts.size() / 2];
        double mean = static_cast<double>(totalKeys) / counts.size();
        
        // Calculate standard deviation
        double variance = 0.0;
        for (int count : counts) {
            variance += (count - mean) * (count - mean);
        }
        double stddev = std::sqrt(variance / counts.size());
        
        LOG_INFO("=== {} ===", phase);
        LOG_INFO("Servers: {}, Keys: {}", counts.size(), totalKeys);
        LOG_INFO("Distribution - Min: {}, Max: {}, Median: {}, Mean: {}, StdDev: {}",
                 min, max, median, mean, stddev);
        LOG_INFO("Balance ratio: {}% (max/mean)", (max / mean) * 100.0);
        LOG_INFO("Imbalance factor: {}", stddev / mean);
    };
    
    // Generate test keys
    LOG_INFO("Generating {} test keys...", numKeys);
    auto keys = GenerateKeys(numKeys, "key_");
    
    // Phase 1: 100 servers
    LOG_INFO("\n--- Phase 1: Initial {} servers ---", initialServers);
    std::vector<std::string> servers100;
    for (int i = 0; i < initialServers; i++) {
        servers100.push_back("cacheserver-" + std::to_string(i) + ".azureml.svc.cluster.local");
    }
    client.m_spConnectionManager->UpdateCacheServers(servers100);
    
    std::unordered_map<std::string, std::string> keyToServer;
    std::unordered_map<std::string, int> distribution100;
    
    for (const auto& key : keys) {
        auto server = client.m_spConnectionManager->GetCacheServerHostname(key);
        ASSERT_FALSE(server.empty());
        keyToServer[key] = server;
        distribution100[server]++;
    }
    
    // Verify all servers got some keys
    ASSERT_EQ(distribution100.size(), initialServers);
    for (const auto& server : servers100) {
        ASSERT_GT(distribution100[server], 0) << "Server " << server << " got no keys";
    }
    
    printDistribution("Phase 1: 100 servers", distribution100, numKeys);
    
    // Phase 2: Scale UP by 20 servers (to 120 total)
    LOG_INFO("\n--- Phase 2: Scale up +20 servers (100 -> 120) ---");
    std::vector<std::string> servers120 = servers100;
    for (int i = initialServers; i < initialServers + 20; i++) {
        servers120.push_back("cacheserver-" + std::to_string(i) + ".azureml.svc.cluster.local");
    }
    client.m_spConnectionManager->UpdateCacheServers(servers120);
    
    std::unordered_map<std::string, int> distribution120;
    int remappedKeys = 0;
    
    for (const auto& key : keys) {
        auto newServer = client.m_spConnectionManager->GetCacheServerHostname(key);
        ASSERT_FALSE(newServer.empty());
        distribution120[newServer]++;
        
        if (keyToServer[key] != newServer) {
            remappedKeys++;
            keyToServer[key] = newServer; // update mapping
        }
    }
    
    ASSERT_EQ(distribution120.size(), 120);
    
    printDistribution("Phase 2: 120 servers", distribution120, numKeys);
    
    double remapPercentage = (static_cast<double>(remappedKeys) / numKeys) * 100.0;
    LOG_INFO("Keys remapped: {} ({}%)", remappedKeys, remapPercentage);
    
    // Theoretical optimal remap: 20/120 = 16.67% of keys should move
    double theoreticalRemap = (20.0 / 120.0) * 100.0;
    LOG_INFO("Theoretical optimal remap: {}%", theoreticalRemap);
    LOG_INFO("Remap efficiency: {}%", (theoreticalRemap / remapPercentage) * 100.0);
    
    // Assert remap is reasonable (should be close to theoretical 16.67%)
    ASSERT_LT(remapPercentage, 25.0) << "Too many keys remapped on scale-up";
    ASSERT_GT(remapPercentage, 10.0) << "Too few keys remapped (not balanced)";
    
    // Phase 3: Scale DOWN by 40 servers (120 -> 80)
    LOG_INFO("\n--- Phase 3: Scale down -40 servers (120 -> 80) ---");
    std::vector<std::string> servers80;
    for (int i = 0; i < 80; i++) {
        servers80.push_back("cacheserver-" + std::to_string(i) + ".azureml.svc.cluster.local");
    }
    client.m_spConnectionManager->UpdateCacheServers(servers80);
    
    std::unordered_map<std::string, int> distribution80;
    remappedKeys = 0;
    
    for (const auto& key : keys) {
        auto newServer = client.m_spConnectionManager->GetCacheServerHostname(key);
        ASSERT_FALSE(newServer.empty());
        ASSERT_TRUE(std::find(servers80.begin(), servers80.end(), newServer) != servers80.end())
            << "Key mapped to removed server";
        distribution80[newServer]++;
        
        if (keyToServer[key] != newServer) {
            remappedKeys++;
            keyToServer[key] = newServer;
        }
    }
    
    ASSERT_EQ(distribution80.size(), 80);
    
    printDistribution("Phase 3: 80 servers", distribution80, numKeys);
    
    remapPercentage = (static_cast<double>(remappedKeys) / numKeys) * 100.0;
    LOG_INFO("Keys remapped: {} ({}%)", remappedKeys, remapPercentage);
    
    // Theoretical optimal remap: 40/120 = 33.33% of keys should move
    theoreticalRemap = (40.0 / 120.0) * 100.0;
    LOG_INFO("Theoretical optimal remap: {}%", theoreticalRemap);
    LOG_INFO("Remap efficiency: {}%", (theoreticalRemap / remapPercentage) * 100.0);
    
    // Assert remap is reasonable (should be close to theoretical 33.33%)
    ASSERT_LT(remapPercentage, 45.0) << "Too many keys remapped on scale-down";
    ASSERT_GT(remapPercentage, 25.0) << "Too few keys remapped";
}
//---------------------------------------------------------------------------------------------------------------------