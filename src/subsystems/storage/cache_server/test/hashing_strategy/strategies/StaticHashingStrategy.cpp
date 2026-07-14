///--------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.
//---------------------------------------------------------------------------------------------------------------------
#include "StaticHashingStrategy.h"
#include <bits/stdc++.h>

std::string StaticHashingStrategy::GetServer(const std::string& key) const {
    auto hash_val = m_hashFunction->Hash64(key);
    return m_servers[hash_val % m_servers.size()];
}

void StaticHashingStrategy::UpdateServers(const std::vector<std::string>& servers) {
    m_servers = servers;
}

std::string StaticHashingStrategy::GetStrategyName() const {
    return "StaticHashing";
}