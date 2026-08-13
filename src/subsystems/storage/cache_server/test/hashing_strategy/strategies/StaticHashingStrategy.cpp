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