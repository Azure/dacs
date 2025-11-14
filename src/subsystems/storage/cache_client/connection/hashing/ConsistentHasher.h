//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
#pragma once
#include "BaseHasher.h"

#include <string>
#include <vector>
#include <map>
#include <shared_mutex>
//---------------------------------------------------------------------------------------------------------------------
class ConsistentHasher: public BaseHasher {
public:
    ConsistentHasher(uint64_t virtualNodes = DEFAULT_VIRTUAL_NODES) : m_virtualNodes(virtualNodes) {}  
    std::string GetServer(const std::string& key) const override;
    void UpdateServers(const std::vector<std::string>& servers) override;

    std::vector<std::string> GetServerList() const;
    uint64_t GetVirtualNodeCount() const { return m_virtualNodes; }

private:
    void AddServerToRing(const std::string& server, std::map<uint64_t, std::string>& ring);
    void RemoveServerFromRing(const std::string& server, std::map<uint64_t, std::string>& ring);
    static uint64_t HexToUint64(const std::string& hexString);

    const uint64_t m_virtualNodes;
    std::map<uint64_t, std::string> m_ring;  // SHA256 hex hash -> server hostname
    mutable std::shared_mutex m_mutex;

    static constexpr uint64_t DEFAULT_VIRTUAL_NODES = 750; // Default virtual nodes per server
};
//---------------------------------------------------------------------------------------------------------------------