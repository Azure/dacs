//---------------------------------------------------------------------------------------------------------------------
// Copyright (c) Microsoft Corporation.  All rights reserved.
//---------------------------------------------------------------------------------------------------------------------
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