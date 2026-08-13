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

#include "IHashingStrategy.h"
#include "HashFunctions.h"
#include <bits/stdc++.h>

class ConsistentHashingStrategy : public IHashingStrategy {
public:
    ConsistentHashingStrategy(size_t virtualNodes = 750, std::unique_ptr<IHashFunction> hashFunc = std::make_unique<FNV1aHash>())
        : m_virtualNodes(virtualNodes), m_hashFunction(std::move(hashFunc)) {}

    std::vector<std::string> GetServerList() const override {
         return m_servers; 
    }

    void SetHashFunction(std::unique_ptr<IHashFunction> hashFunc) override {
        m_hashFunction = std::move(hashFunc);
    }

    std::string GetHashFunctionName() const override {
        return m_hashFunction ? m_hashFunction->GetName() : "None";
    }

    std::unique_ptr<IHashingStrategy> CreateInstance(std::unique_ptr<IHashFunction> hashFunc) const override {
        return std::make_unique<ConsistentHashingStrategy>(m_virtualNodes, std::move(hashFunc));
    }

    std::string GetStrategyName() const override;
    std::string GetServer(const std::string& key) const override;
    
    void UpdateServers(const std::vector<std::string>& servers) override;

private:
    std::vector<std::string> m_servers;
    size_t m_virtualNodes; // Number of virtual nodes per server
        
    std::unique_ptr<IHashFunction> m_hashFunction;
    std::map<size_t, std::string> m_ring; // hash -> server

    void AddServerToRing(const std::string& server);
    void RemoveServerFromRing(const std::string& server);

};