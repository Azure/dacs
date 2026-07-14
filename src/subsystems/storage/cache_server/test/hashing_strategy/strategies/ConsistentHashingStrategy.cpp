///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "ConsistentHashingStrategy.h"
#include <bits/stdc++.h>

std::string ConsistentHashingStrategy::GetServer(const std::string& key) const {
    if (m_servers.empty()) return "";
    
    auto hash_val = m_hashFunction->Hash64(key);
    auto it = m_ring.lower_bound(hash_val);
    if (it == m_ring.end()) {
        it = m_ring.begin();   // circle back to the first node
    }
    return it->second;
}

void ConsistentHashingStrategy::UpdateServers(const std::vector<std::string>& servers) {
    std::set<std::string> oldServerSet(m_servers.begin(), m_servers.end());
    std::set<std::string> newServerSet(servers.begin(), servers.end());
    std::vector<std::string> serversToRemove, serversToAdd;
    
    // Find servers to remove
    std::set_difference(oldServerSet.begin(), oldServerSet.end(),
                       newServerSet.begin(), newServerSet.end(),
                       std::back_inserter(serversToRemove));
    
    // Find servers to add  
    std::set_difference(newServerSet.begin(), newServerSet.end(),
                       oldServerSet.begin(), oldServerSet.end(),
                       std::back_inserter(serversToAdd));
    
    // Remove virtual nodes for removed servers
    for (const auto& server : serversToRemove) {
        RemoveServerFromRing(server);
    }
    
    // Add virtual nodes for new servers
    for (const auto& server : serversToAdd) {
        AddServerToRing(server);
    }
    
    m_servers = servers;
}

std::string ConsistentHashingStrategy::GetStrategyName() const {
    return "ConsistentHashing";
}

void ConsistentHashingStrategy::AddServerToRing(const std::string& server) {
    size_t serverHash = m_hashFunction->Hash64(server);
    for (size_t v = 0; v < m_virtualNodes; ++v) {
        std::string vnodeKey = server + "_" + std::to_string(serverHash * 31337 + v * 7919);
        size_t virtual_node_hash = m_hashFunction->Hash64(vnodeKey);
        m_ring[virtual_node_hash] = server;
    }
}

void ConsistentHashingStrategy::RemoveServerFromRing(const std::string& server) {
    size_t serverHash = m_hashFunction->Hash64(server);
    for (size_t v = 0; v < m_virtualNodes; ++v) {
        std::string vnodeKey = server + "_" + std::to_string(serverHash * 31337 + v * 7919);
        size_t virtual_node_hash = m_hashFunction->Hash64(vnodeKey);
        m_ring.erase(virtual_node_hash);
    }
}