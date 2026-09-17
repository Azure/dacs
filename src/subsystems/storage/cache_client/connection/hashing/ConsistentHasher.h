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
#include "BaseHasher.h"

#include <string>
#include <vector>
#include <map>
#include <shared_mutex>
//---------------------------------------------------------------------------------------------------------------------
class ConsistentHasher: public BaseHasher {
public:
    ConsistentHasher(uint64_t virtualNodes = DEFAULT_VIRTUAL_NODES) : m_virtualNodes(virtualNodes) {}  
    std::string GetServer(std::string_view key) const override;
    void UpdateServers(const std::vector<std::string>& servers) override;

    std::vector<std::string> GetServerList() const;
    uint64_t GetVirtualNodeCount() const { return m_virtualNodes; }

private:
    void AddServerToRing(const std::string& server, std::map<uint64_t, std::string>& ring);
    void RemoveServerFromRing(const std::string& server, std::map<uint64_t, std::string>& ring);
    static uint64_t HexToUint64(std::string_view hexString);

    const uint64_t m_virtualNodes;
    std::map<uint64_t, std::string> m_ring;  // SHA256 hex hash -> server hostname
    mutable std::shared_mutex m_mutex;

    static constexpr uint64_t DEFAULT_VIRTUAL_NODES = 750; // Default virtual nodes per server
};
//---------------------------------------------------------------------------------------------------------------------