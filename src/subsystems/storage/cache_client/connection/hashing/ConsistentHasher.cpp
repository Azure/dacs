//--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#include "ConsistentHasher.h"
#include "CommonLogger.h"
#include "CacheUtils.h"
#include <algorithm>
#include <set>
#include <cstdint>
//---------------------------------------------------------------------------------------------------------------------
uint64_t ConsistentHasher::HexToUint64(const std::string& hexString) {
    // XOR-fold all 256 bits into 64 bits to preserve full hash entropy.
    // Splitting 256 bits into four 64-bit chunks and XOR them together.
    uint64_t result = 0;
    
    // Process in 4 chunks: bits [0-63], [64-127], [128-191], [192-255]
    for (size_t i = 0; i < 64; i += 16) {
        result ^= std::stoull(hexString.substr(i, 16), nullptr, 16);
    }
    
    return result;
}
//---------------------------------------------------------------------------------------------------------------------
std::string ConsistentHasher::GetServer(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if (m_servers.empty()) return "";

    uint64_t hash_val = HexToUint64(cacheutils::GetSHA256Hex(key));
    auto it = m_ring.lower_bound(hash_val);
    if (it == m_ring.end()) {
        it = m_ring.begin();   // circle back to the first node
    }
    return it->second;
}
//---------------------------------------------------------------------------------------------------------------------
void ConsistentHasher::UpdateServers(const std::vector<std::string>& servers) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    LOG_DEBUG("ConsistentHasher: Updating servers from {} to {} servers", 
              m_servers.size(), servers.size());

    if (m_servers == servers) {
        LOG_DEBUG("ConsistentHasher: Server list unchanged, skipping update");
        return;
    }

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

    std::map<uint64_t, std::string> newRing = m_ring;
    
    try {
        // Remove virtual nodes for removed servers
        for (const auto& server : serversToRemove) {
            RemoveServerFromRing(server, newRing);
        }
        
        // Add virtual nodes for new servers
        for (const auto& server : serversToAdd) {
            AddServerToRing(server, newRing);
        }
        m_ring = std::move(newRing);
        m_servers = servers;
        LOG_INFO("ConsistentHasher: Successfully updated to {} servers with {} virtual nodes", 
                m_servers.size(), m_ring.size());
    } catch (const std::exception& ex) {
        LOG_ERROR("ConsistentHasher: Failed to update servers: {}. Keeping old configuration.", ex.what());
    }
}
//---------------------------------------------------------------------------------------------------------------------
void ConsistentHasher::AddServerToRing(const std::string& server, std::map<uint64_t, std::string>& ring) {
    for (uint64_t v = 0; v < m_virtualNodes; ++v) {
        std::string vnodeKey = server + "_" + std::to_string(v);
        uint64_t virtualNodeHash = HexToUint64(cacheutils::GetSHA256Hex(vnodeKey));
        ring[virtualNodeHash] = server;
    }
}
//---------------------------------------------------------------------------------------------------------------------
void ConsistentHasher::RemoveServerFromRing(const std::string& server, std::map<uint64_t, std::string>& ring) {
    for (uint64_t v = 0; v < m_virtualNodes; ++v) {
        std::string vnodeKey = server + "_" + std::to_string(v);
        uint64_t virtualNodeHash = HexToUint64(cacheutils::GetSHA256Hex(vnodeKey));
        ring.erase(virtualNodeHash);
    }
}
//---------------------------------------------------------------------------------------------------------------------
std::vector<std::string> ConsistentHasher::GetServerList() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_servers;
}
//---------------------------------------------------------------------------------------------------------------------