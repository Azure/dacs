///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
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