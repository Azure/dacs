///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "RendezvousHashingStrategy.h"

uint64_t hash_mix64(uint64_t h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

// Combine two hash values with high avalanche effect
uint64_t combine_hashes(uint64_t keyHash, uint64_t serverHash) {
    uint64_t combined = keyHash ^ serverHash;
    combined ^= (serverHash >> 16) ^ (keyHash << 16);
    return hash_mix64(combined);
}

std::string RendezvousHashingStrategy::GetServer(const std::string& key) const {    
    std::string bestServer;
    uint64_t highestWeight = 0;
    bool firstServer = true;
    
    uint64_t keyHash = m_hashFunction->Hash64(key);

    for (const auto& currentServer : m_sortedServers) {
        uint64_t serverHash = m_hashFunction->Hash64(currentServer);
        uint64_t weight = combine_hashes(keyHash, serverHash);
        
        if (firstServer || weight > highestWeight) {
            highestWeight = weight;
            bestServer = currentServer;
            firstServer = false;
        }
    }
    return bestServer;
}

void RendezvousHashingStrategy::UpdateServers(const std::vector<std::string>& servers) {
    m_servers = servers;
    m_sortedServers = servers;
    std::sort(m_sortedServers.begin(), m_sortedServers.end());
}

std::string RendezvousHashingStrategy::GetStrategyName() const {
    return "RendezvousHashing";
}