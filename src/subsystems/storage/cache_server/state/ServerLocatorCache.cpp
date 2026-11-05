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
#include "ServerLocatorCache.h"
#include "FlatMapIndex.h"
#include "FlatRadixIndex.h"
#include "LoggingApi.h"
#include "snapshot.pb.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------

namespace kvclient
{

static std::unique_ptr<LocatorCache> s_spServerLocatorCache = std::make_unique<LocatorCache>();

LocatorCache& GetServerLocatorCache()
{
    return *s_spServerLocatorCache;
}

void InitServerLocatorCache(uint32_t nNodeId, uint32_t nNumNodes)
{
    s_spServerLocatorCache->Init(nNodeId, nNumNodes);
}

void InitServerLocatorCache(const std::string& strIndexBackend,
                            uint32_t nNodeId, uint32_t nNumNodes)
{
    std::string strNormalized = strIndexBackend;
    std::transform(strNormalized.begin(), strNormalized.end(), strNormalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::unique_ptr<ILocatorIndex> pIndex;
    if (strNormalized == "flatradix")
    {
        LOG_INFO("ServerLocatorCache: using FlatRadixIndex backend");
        pIndex = std::make_unique<FlatRadixIndex>();
    }
    else
    {
        if (!strNormalized.empty() && strNormalized != "flatmap")
        {
            LOG_WARN("ServerLocatorCache: unknown locator index backend '{}', falling back to FlatMapIndex",
                     strIndexBackend);
        }
        else
        {
            LOG_INFO("ServerLocatorCache: using FlatMapIndex backend");
        }
        pIndex = std::make_unique<FlatMapIndex>();
    }

    s_spServerLocatorCache = std::make_unique<LocatorCache>(std::move(pIndex));
    s_spServerLocatorCache->Init(nNodeId, nNumNodes);
}

void ResetServerLocatorCacheForTest(std::unique_ptr<ILocatorIndex> pIndex,
                                    uint32_t nNodeId, uint32_t nNumNodes)
{
    s_spServerLocatorCache = std::make_unique<LocatorCache>(std::move(pIndex));
    s_spServerLocatorCache->Init(nNodeId, nNumNodes);
}

//---------------------------------------------------------------------------------------------------------------------
size_t AddLocatorEntriesToSnapshot(snapshot::Snapshot& snapshot)
{
    size_t nAdded = 0;
    GetServerLocatorCache().ForEachEntry([&snapshot, &nAdded](const LocatorEntry& entry) {
        snapshot::LocatorSnapshotEntry* pEntry = snapshot.add_locatorentries();
        pEntry->set_key(entry.m_strKey);
        pEntry->set_parentkey(entry.m_strParentKey);
        pEntry->set_rootkey(entry.m_strRootKey);
        for (const auto& replica : entry.m_vReplicas)
        {
            snapshot::LocatorReplica* pReplica = pEntry->add_replicas();
            pReplica->set_tier(static_cast<uint32_t>(replica.m_tier));
            pReplica->set_nodeid(replica.m_nNodeId);
            pReplica->set_expiresatms(replica.m_nExpiresAt);
            pReplica->set_lastrefreshedms(replica.m_nLastRefreshed);
            pReplica->set_contentsize(replica.m_nContentSize);
        }
        ++nAdded;
    });
    return nAdded;
}

//---------------------------------------------------------------------------------------------------------------------
size_t RestoreLocatorEntriesFromSnapshot(const snapshot::Snapshot& snapshot, uint64_t nNowMs)
{
    std::vector<LocatorEntry> vEntries;
    vEntries.reserve(static_cast<size_t>(snapshot.locatorentries_size()));
    for (const auto& savedEntry : snapshot.locatorentries())
    {
        LocatorEntry entry(savedEntry.key(), nNowMs, savedEntry.parentkey(), savedEntry.rootkey());
        for (const auto& savedReplica : savedEntry.replicas())
        {
            if (savedReplica.tier() > static_cast<uint32_t>(ReplicaTier::Blob))
            {
                LOG_WARN("ServerLocatorCache: skipping replica of {} with unknown tier {}",
                         savedEntry.key(), savedReplica.tier());
                continue;
            }
            ContentReplica replica(savedReplica.nodeid(), savedReplica.expiresatms(),
                                   savedReplica.lastrefreshedms(), savedReplica.contentsize());
            replica.m_tier = static_cast<ReplicaTier>(savedReplica.tier());
            entry.m_vReplicas.push_back(replica);
        }
        vEntries.push_back(std::move(entry));
    }
    return GetServerLocatorCache().Restore(std::move(vEntries), nNowMs);
}

} // namespace kvclient
