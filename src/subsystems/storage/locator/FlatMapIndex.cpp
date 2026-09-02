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
#include "FlatMapIndex.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
//---------------------------------------------------------------------------------------------------------------------
//
// FlatMapIndex is the original flat storage, ported verbatim behind ILocatorIndex. Every
// operation takes exactly one bucket lock; the parent/root storage keys are recorded
// on the entry but no child links are maintained, so there is no two-lock path and no
// lock-ordering invariant to preserve.
//---------------------------------------------------------------------------------------------------------------------

namespace kvclient
{

// Number of lock-striped buckets for concurrent access (matches the original storage).
static constexpr uint32_t NUM_LOCATOR_BUCKETS = 64;

//---------------------------------------------------------------------------------------------------------------------
FlatMapIndex::FlatMapIndex()
    : m_vEntries(NUM_LOCATOR_BUCKETS)
    , m_vMutexes(NUM_LOCATOR_BUCKETS)
{
}

//---------------------------------------------------------------------------------------------------------------------
void FlatMapIndex::Register(const std::string& strKey, const std::string& strParentKey,
                            const std::string& strRootKey, const ContentReplica& replica)
{
    const uint32_t nBucket = GetBucketIndex(strKey);
    std::lock_guard<std::mutex> lock(m_vMutexes[nBucket]);

    auto& map = m_vEntries[nBucket];
    auto it = map.find(strKey);

    if (it == map.end())
    {
        // New entry
        LocatorEntry entry(strKey, NowMs(), strParentKey, strRootKey);
        entry.m_vReplicas.push_back(replica);
        map.emplace(strKey, std::move(entry));
        return;
    }

    // Existing entry - keep tree fields current and update or add the replica.
    it->second.m_strParentKey = strParentKey;
    it->second.m_strRootKey = strRootKey;

    auto& replicas = it->second.m_vReplicas;
    auto replicaIt = std::find_if(replicas.begin(), replicas.end(),
        [&replica](const ContentReplica& r) { return r.SameSlotAs(replica); });

    if (replicaIt != replicas.end())
    {
        replicaIt->m_nExpiresAt = replica.m_nExpiresAt;
        replicaIt->m_nLastRefreshed = replica.m_nLastRefreshed;
    }
    else
    {
        replicas.push_back(replica);
    }
}

//---------------------------------------------------------------------------------------------------------------------
std::optional<LocatorEntry> FlatMapIndex::Lookup(const std::string& strKey) const
{
    const uint32_t nBucket = GetBucketIndex(strKey);
    std::lock_guard<std::mutex> lock(m_vMutexes[nBucket]);

    const auto& map = m_vEntries[nBucket];
    auto it = map.find(strKey);
    if (it == map.end())
    {
        return std::nullopt;
    }
    return it->second;
}

//---------------------------------------------------------------------------------------------------------------------
bool FlatMapIndex::Refresh(const std::string& strKey, uint32_t nNodeId, uint64_t nNewExpiresAt)
{
    const uint32_t nBucket = GetBucketIndex(strKey);
    std::lock_guard<std::mutex> lock(m_vMutexes[nBucket]);

    auto& map = m_vEntries[nBucket];
    auto it = map.find(strKey);
    if (it == map.end())
    {
        return false;
    }

    auto& replicas = it->second.m_vReplicas;
    auto replicaIt = std::find_if(replicas.begin(), replicas.end(),
        [nNodeId](const ContentReplica& r) { return r.m_tier == ReplicaTier::Cache && r.m_nNodeId == nNodeId; });

    if (replicaIt == replicas.end())
    {
        return false;
    }

    replicaIt->m_nExpiresAt = nNewExpiresAt;
    replicaIt->m_nLastRefreshed = NowMs();
    return true;
}

//---------------------------------------------------------------------------------------------------------------------
bool FlatMapIndex::Unregister(const std::string& strKey, uint32_t nNodeId)
{
    const uint32_t nBucket = GetBucketIndex(strKey);
    std::lock_guard<std::mutex> lock(m_vMutexes[nBucket]);

    auto& map = m_vEntries[nBucket];
    auto it = map.find(strKey);
    if (it == map.end())
    {
        return false;
    }

    auto& replicas = it->second.m_vReplicas;
    auto replicaIt = std::find_if(replicas.begin(), replicas.end(),
        [nNodeId](const ContentReplica& r) { return r.m_tier == ReplicaTier::Cache && r.m_nNodeId == nNodeId; });

    if (replicaIt == replicas.end())
    {
        return false;
    }

    replicas.erase(replicaIt);

    // Remove the entry entirely if no replicas remain.
    if (replicas.empty())
    {
        map.erase(it);
    }

    return true;
}

//---------------------------------------------------------------------------------------------------------------------
size_t FlatMapIndex::PurgeExpired()
{
    const uint64_t nNowMs = NowMs();
    size_t nTotalPurged = 0;

    for (uint32_t i = 0; i < NUM_LOCATOR_BUCKETS; ++i)
    {
        std::lock_guard<std::mutex> lock(m_vMutexes[i]);
        auto& map = m_vEntries[i];

        auto it = map.begin();
        while (it != map.end())
        {
            nTotalPurged += it->second.PurgeExpiredReplicas(nNowMs);

            if (!it->second.HasReplicas())
            {
                it = map.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    return nTotalPurged;
}

//---------------------------------------------------------------------------------------------------------------------
PrefixLookupResult FlatMapIndex::PrefixLookup(const std::string& strBaseParentKey,
                                              std::span<const std::string> vStorageKeys,
                                              uint64_t nNowMs) const
{
    (void)strBaseParentKey;
    (void)nNowMs;
    (void)vStorageKeys;
    assert(false && "PrefixLookup is not supported by FlatMapIndex.");

    // Non-breaking release-build behavior: report zero matches (matched_count == 0), 
    // degrading gracefully to the legacy path.
    return PrefixLookupResult{};
}

//---------------------------------------------------------------------------------------------------------------------
size_t FlatMapIndex::GetEntryCount() const
{
    size_t nCount = 0;
    for (uint32_t i = 0; i < NUM_LOCATOR_BUCKETS; ++i)
    {
        std::lock_guard<std::mutex> lock(m_vMutexes[i]);
        nCount += m_vEntries[i].size();
    }
    return nCount;
}

//---------------------------------------------------------------------------------------------------------------------
size_t FlatMapIndex::GetReplicaCount() const
{
    size_t nCount = 0;
    for (uint32_t i = 0; i < NUM_LOCATOR_BUCKETS; ++i)
    {
        std::lock_guard<std::mutex> lock(m_vMutexes[i]);
        for (const auto& [key, entry] : m_vEntries[i])
        {
            nCount += entry.m_vReplicas.size();
        }
    }
    return nCount;
}

//---------------------------------------------------------------------------------------------------------------------
uint32_t FlatMapIndex::GetBucketIndex(const std::string& strKey) const
{
    return static_cast<uint32_t>(std::hash<std::string>{}(strKey) % NUM_LOCATOR_BUCKETS);
}

//---------------------------------------------------------------------------------------------------------------------
uint64_t FlatMapIndex::NowMs()
{
    // Replica expiry is a cross-host absolute timestamp (stamped by the client, checked by the
    // server), so it must use system_clock (shared UNIX epoch), not steady_clock (per-host).
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

} // namespace kvclient
