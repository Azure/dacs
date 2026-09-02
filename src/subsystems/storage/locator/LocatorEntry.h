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
#ifndef LOCATORENTRY_H_
#define LOCATORENTRY_H_
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------

// Which tier a replica lives in.
//   Cache - a warm copy on a cache node; short TTL (seconds), refreshed by the node.
//   Blob  - the durable copy in Azure Blob. Its location is NOT stored here; it is derived
//           from (prefix_hash, partition_key) via ComputeBlobName at read time. It is tracked
//           by a conservative soft-TTL (days) that the refresh-hot sweep bumps for blocks still
//           hot in cache, so the locator never advertises a blob the lifecycle policy already
//           deleted.
enum class ReplicaTier : uint8_t
{
    Cache = 0,
    Blob  = 1,
};

struct ContentReplica
{
    ReplicaTier m_tier = ReplicaTier::Cache;  // Which tier this replica is in
    uint32_t m_nNodeId;          // Cache tier: content node storing this replica (unused for Blob)
    uint64_t m_nExpiresAt;       // Expiry (epoch ms): cache TTL, or blob soft-TTL for the Blob tier
    uint64_t m_nLastRefreshed;   // When the TTL was last refreshed
    uint64_t m_nContentSize;     // Logical size of stored value in bytes (0 = unknown)

    ContentReplica() : m_nNodeId(0), m_nExpiresAt(0), m_nLastRefreshed(0), m_nContentSize(0) {}

    // Cache-tier replica (default tier). Existing call sites are unchanged.
    ContentReplica(uint32_t nNodeId, uint64_t nExpiresAt, uint64_t nLastRefreshed,
                   uint64_t nContentSize = 0)
        : m_nNodeId(nNodeId), m_nExpiresAt(nExpiresAt), m_nLastRefreshed(nLastRefreshed)
        , m_nContentSize(nContentSize)
    {
    }

    // Durable blob replica. No cache node; `nSoftExpiresAt` is a conservative soft-TTL set to
    // expire before the blob's real last-access lifecycle deletion. The blob location is derived
    // at read time, so it is not stored here.
    static ContentReplica MakeBlobReplica(uint64_t nSoftExpiresAt, uint64_t nLastRefreshed,
                                          uint64_t nContentSize = 0)
    {
        ContentReplica replica(/*nNodeId=*/0, nSoftExpiresAt, nLastRefreshed, nContentSize);
        replica.m_tier = ReplicaTier::Blob;
        return replica;
    }

    // Applies to both tiers: a cache replica past its TTL, or a blob replica past its soft-TTL.
    bool IsExpired(uint64_t nNowMs) const { return m_nExpiresAt <= nNowMs; }

    // Whether `other` should update THIS replica in place rather than be appended as a new one.
    // Cache replicas are keyed by their node id (one replica per node); there is at most one
    // durable Blob replica per entry, so any two Blob replicas share the same slot. This keeps a
    // blob replica (node id 0) from colliding with a cache replica that happens to be on node 0.
    bool SameSlotAs(const ContentReplica& other) const
    {
        if (m_tier != other.m_tier)
            return false;
        return m_tier == ReplicaTier::Cache ? (m_nNodeId == other.m_nNodeId) : true;
    }
};

// NOTE (future memory optimization):
// Every key stored here - m_strKey and the m_strParentKey / m_strRootKey links - is a
// "b:%016llx" storage_key string (~18 bytes plus a heap allocation each). Each is a
// bijection with a uint64 prefix_hash, so the entry could hold uint64 keys instead of
// strings: ~24B+ saved per entry and per link, and no per-key allocations. That would key
// the locator map by uint64 and push string<->int conversion to the caller/server boundary.
struct LocatorEntry
{
    std::string m_strKey;                          // KV cache key
    std::vector<ContentReplica> m_vReplicas;       // Per-node replica info with TTL tracking
    uint64_t m_nTimestamp;                         // Locator creation time (epoch ms)

    // Prefix-tree structure (set at registration time). Linkage is by storage_key so the
    // locator never has to parse a key back into a prefix_hash; an empty key means "none"
    // (i.e. a root block with no parent/root above it).
    std::string m_strParentKey;                    // storage_key of the parent block ("" for root)
    std::string m_strRootKey;                      // storage_key of the chain's root block ("" for root)
    std::vector<std::string> m_vChildren;          // storage_keys of direct children registered so far

    LocatorEntry() : m_nTimestamp(0) {}

    LocatorEntry(const std::string& strKey, uint64_t nTimestamp,
                 const std::string& strParentKey = std::string{},
                 const std::string& strRootKey = std::string{})
        : m_strKey(strKey), m_nTimestamp(nTimestamp)
        , m_strParentKey(strParentKey), m_strRootKey(strRootKey)
    {
    }

    // Remove expired replicas, return number removed
    size_t PurgeExpiredReplicas(uint64_t nNowMs)
    {
        size_t nOriginalSize = m_vReplicas.size();
        m_vReplicas.erase(
            std::remove_if(m_vReplicas.begin(), m_vReplicas.end(),
                [nNowMs](const ContentReplica& replica) { return replica.IsExpired(nNowMs); }),
            m_vReplicas.end());
        return nOriginalSize - m_vReplicas.size();
    }

    bool HasReplicas() const { return !m_vReplicas.empty(); }

    // --- Tier-aware queries (durable-blob residency) --------------------------

    // True if any non-expired cache-tier replica exists (i.e. the block is warm in cache).
    bool HasLiveCacheReplica(uint64_t nNowMs) const
    {
        for (const auto& replica : m_vReplicas)
            if (replica.m_tier == ReplicaTier::Cache && !replica.IsExpired(nNowMs))
                return true;
        return false;
    }

    // The live durable blob replica, or nullptr. Lets Lookup answer residency = Blob once the
    // cache copies have aged out but the durable copy is still present.
    const ContentReplica* GetLiveBlobReplica(uint64_t nNowMs) const
    {
        for (const auto& replica : m_vReplicas)
            if (replica.m_tier == ReplicaTier::Blob && !replica.IsExpired(nNowMs))
                return &replica;
        return nullptr;
    }

    bool HasLiveBlobReplica(uint64_t nNowMs) const { return GetLiveBlobReplica(nNowMs) != nullptr; }

    // The single replica a lookup should advertise for this block, encoding the tier preference
    // in one place: a live cache replica if the block is still warm (cache read is cheapest),
    // otherwise the live durable blob replica, otherwise nullptr (a genuine miss). Callers use the
    // returned replica's m_tier to decide residency (Cache vs Blob).
    const ContentReplica* SelectLiveReplica(uint64_t nNowMs) const
    {
        for (const auto& replica : m_vReplicas)
            if (replica.m_tier == ReplicaTier::Cache && !replica.IsExpired(nNowMs))
                return &replica;
        return GetLiveBlobReplica(nNowMs);
    }
};

//---------------------------------------------------------------------------------------------------------------------
#endif // LOCATORENTRY_H_
