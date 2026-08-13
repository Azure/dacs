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

#include "StaticModuloHasher.h"
#include "CommonLogger.h"
#include "CacheUtils.h"
#include <cstdint>
//---------------------------------------------------------------------------------------------------------------------
std::string StaticModuloHasher::GetServer(const std::string& key) const {
    if (m_servers.empty()) {
        return "";
    }
    uint32_t serverIndex = cacheutils::GetServerIndex(key.c_str(), m_servers.size());
    return m_servers[serverIndex];
}
//---------------------------------------------------------------------------------------------------------------------
void StaticModuloHasher::UpdateServers(const std::vector<std::string>& servers) {
    m_servers = servers;
}
//---------------------------------------------------------------------------------------------------------------------