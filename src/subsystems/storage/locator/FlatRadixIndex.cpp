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
#include "FlatRadixIndex.h"
#include "LoggingApi.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <utility>
//---------------------------------------------------------------------------------------------------------------------
//
// Lock-ordering invariant
// -----------------------
// Entries are sharded across NUM_LOCATOR_BUCKETS buckets, each guarded by its own mutex.
// A registration/unregistration touches a child entry and its parent entry, which may live
// in different buckets. To avoid ABBA deadlock:
//
//   * Operations that mutate a child entry and its parent entry TOGETHER (Register,
//     Unregister) hold both bucket locks at once, always acquiring the LOWER bucket index
//     first, then the higher. When child and parent hash to the same bucket, a single lock
//     is taken.
//
//   * Operations that mutate ONLY the parent's child list (the cleanup performed during
//     PurgeExpired) hold a single bucket lock - the parent's - and therefore cannot
//     deadlock. PurgeExpired defers those cleanups until after it has released the scanned
//     bucket's lock, so it never holds two bucket locks in non-min order.
//
//   * Cascade delete (CascadeDeleteDescendants) erases an entire subtree so orphaned
//     children can never outlive a deleted parent. It walks the subtree taking a single
//     bucket lock per node; no parent-link fix-up is required because every ancestor in the
//     cascade is being deleted too, so it never holds two bucket locks at once. Callers
//     defer the cascade until after they have released the locks held during deletion.
//
//---------------------------------------------------------------------------------------------------------------------

namespace kvclient
{

namespace
{

//---------------------------------------------------------------------------------------------------------------------
// Insert a new entry or update an existing one, recording its prefix-tree position.
// Caller must hold the lock for the bucket owning strKey.
//---------------------------------------------------------------------------------------------------------------------
void InsertOrUpdateLocked(std::unordered_map<std::string, LocatorEntry>& map,
                          const std::string& strKey, const std::string& strParentKey,
                          const std::string& strRootKey, const ContentReplica& replica, uint64_t nNowMs)
{
    auto it = map.find(strKey);
    if (it == map.end())
    {
        LocatorEntry entry(strKey, nNowMs, strParentKey, strRootKey);
        // Root-hash co-location is validated in LocatorCache::Register (debug-only), which owns
        // this node's ID and the cluster size; the backend has no topology of its own.
        entry.m_vReplicas.push_back(replica);
        map.emplace(strKey, std::move(entry));
        return;
    }

    // The stored parent/root must match what the caller re-registers. storage_key is
    // content-derived, so a key's tree position is fixed; a mismatch means the incoming
    // registration is not the same logical block (a mis-built parent chain or a storage_key
    // hash collision). The cause is irrelevant - we cannot safely attach this replica to a
    // different block, so drop the registration entirely and keep the first-seen entry.
    // TODO: this is silently ignored; ideally Register would surface an error to the caller.
    if (it->second.m_strParentKey != strParentKey || it->second.m_strRootKey != strRootKey)
    {
        LOG_WARN("FlatRadixIndex: skipping re-registration of {} - parent/root ({}/{}) differs "
                 "from stored ({}/{})",
                 strKey, strParentKey, strRootKey, it->second.m_strParentKey, it->second.m_strRootKey);
        return;
    }

    // Existing entry - keep its original tree position (parent/root are unchanged) and only
    // update or add the replica. Slot matching is tier-aware (see ContentReplica::SameSlotAs)
    // so a durable Blob replica and a cache replica on node 0 don't overwrite each other, and a
    // repeat blob register (e.g. the refresh-hot sweep) simply bumps the existing blob soft-TTL.
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
// Push a child's storage_key into its parent's child list (deduplicated).
// Caller must hold the lock for the bucket owning strParentKey.
//---------------------------------------------------------------------------------------------------------------------
void AddChildLocked(std::unordered_map<std::string, LocatorEntry>& parentMap,
                    const std::string& strParentKey, const std::string& strChildKey)
{
    auto it = parentMap.find(strParentKey);
    if (it == parentMap.end())
    {
        // Parent not registered (yet) - the child is still registered independently.
        LOG_WARN("FlatRadixIndex: parent entry {} not found while linking child {}",
                 strParentKey, strChildKey);
        return;
    }

    auto& children = it->second.m_vChildren;
    if (std::find(children.begin(), children.end(), strChildKey) == children.end())
    {
        children.push_back(strChildKey);
    }
}

//---------------------------------------------------------------------------------------------------------------------
// Remove a child's storage_key from its parent's child list, if present.
// Caller must hold the lock for the bucket owning strParentKey.
//---------------------------------------------------------------------------------------------------------------------
void RemoveChildLocked(std::unordered_map<std::string, LocatorEntry>& parentMap,
                       const std::string& strParentKey, const std::string& strChildKey)
{
    auto it = parentMap.find(strParentKey);
    if (it == parentMap.end())
    {
        return;
    }

    auto& children = it->second.m_vChildren;
    children.erase(std::remove(children.begin(), children.end(), strChildKey), children.end());
}

} // namespace

//---------------------------------------------------------------------------------------------------------------------
FlatRadixIndex::FlatRadixIndex()
    : m_vEntries(NUM_LOCATOR_BUCKETS)
    , m_vMutexes(NUM_LOCATOR_BUCKETS)
{
}

//---------------------------------------------------------------------------------------------------------------------
void FlatRadixIndex::Register(const std::string& strKey, const std::string& strParentKey,
                              const std::string& strRootKey, const ContentReplica& replica)
{
    const uint32_t nChildBucket = GetBucketIndex(strKey);
    const uint64_t nNowMs = NowMs();

    // Root block (no parent): single-lock fast path.
    if (strParentKey.empty())
    {
        std::lock_guard<std::mutex> lock(m_vMutexes[nChildBucket]);
        InsertOrUpdateLocked(m_vEntries[nChildBucket], strKey, strParentKey, strRootKey, replica, nNowMs);
        return;
    }

    const uint32_t nParentBucket = GetBucketIndex(strParentKey);

    if (nChildBucket == nParentBucket)
    {
        std::lock_guard<std::mutex> lock(m_vMutexes[nChildBucket]);
        InsertOrUpdateLocked(m_vEntries[nChildBucket], strKey, strParentKey, strRootKey, replica, nNowMs);
        AddChildLocked(m_vEntries[nParentBucket], strParentKey, strKey);
        return;
    }

    // Two distinct buckets - acquire the lower index first (min-bucket ordering).
    const uint32_t nLo = std::min(nChildBucket, nParentBucket);
    const uint32_t nHi = std::max(nChildBucket, nParentBucket);
    std::lock_guard<std::mutex> lockLo(m_vMutexes[nLo]);
    std::lock_guard<std::mutex> lockHi(m_vMutexes[nHi]);

    InsertOrUpdateLocked(m_vEntries[nChildBucket], strKey, strParentKey, strRootKey, replica, nNowMs);
    AddChildLocked(m_vEntries[nParentBucket], strParentKey, strKey);
}

//---------------------------------------------------------------------------------------------------------------------
std::optional<LocatorEntry> FlatRadixIndex::Lookup(const std::string& strKey) const
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
bool FlatRadixIndex::Refresh(const std::string& strKey, uint32_t nNodeId, uint64_t nNewExpiresAt)
{
    const uint32_t nBucket = GetBucketIndex(strKey);
    std::lock_guard<std::mutex> lock(m_vMutexes[nBucket]);

    auto& map = m_vEntries[nBucket];
    auto it = map.find(strKey);
    if (it == map.end())
    {
        return false;
    }

    // TTL refresh targets cache-tier replicas by node id; the durable blob replica has its own
    // soft-TTL (bumped via re-register by the refresh-hot sweep), so it is never touched here.
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
bool FlatRadixIndex::Unregister(const std::string& strKey, uint32_t nNodeId)
{
    const uint32_t nChildBucket = GetBucketIndex(strKey);

    // Peek the entry's parent so we know whether a second (parent) lock is required.
    std::string strParentKey;
    {
        std::lock_guard<std::mutex> lock(m_vMutexes[nChildBucket]);
        const auto& map = m_vEntries[nChildBucket];
        auto it = map.find(strKey);
        if (it == map.end())
        {
            return false;
        }
        strParentKey = it->second.m_strParentKey;
    }

    // Root block (or orphan) - single-lock path.
    if (strParentKey.empty())
    {
        std::vector<std::string> vChildrenToCascade;
        {
            std::lock_guard<std::mutex> lock(m_vMutexes[nChildBucket]);
            auto& map = m_vEntries[nChildBucket];
            auto it = map.find(strKey);
            if (it == map.end())
            {
                return false;
            }

            // Unregister targets a cache-tier replica (e.g. on cache eviction); the durable blob
            // replica is deliberately left in place so a later lookup still resolves residency=Blob.
            auto& replicas = it->second.m_vReplicas;
            auto replicaIt = std::find_if(replicas.begin(), replicas.end(),
                [nNodeId](const ContentReplica& r) { return r.m_tier == ReplicaTier::Cache && r.m_nNodeId == nNodeId; });
            if (replicaIt == replicas.end())
            {
                return false;
            }

            replicas.erase(replicaIt);
            if (replicas.empty())
            {
                // Cascade delete: the whole subtree goes with the parent so no child is
                // left orphaned. Descendants are removed after this lock is released.
                vChildrenToCascade = std::move(it->second.m_vChildren);
                map.erase(it);
            }
        }
        CascadeDeleteDescendants(std::move(vChildrenToCascade));
        return true;
    }

    const uint32_t nParentBucket = GetBucketIndex(strParentKey);

    std::vector<std::string> vChildrenToCascade;
    const auto doUnregister = [&]() -> bool {
        auto& map = m_vEntries[nChildBucket];
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

        // Once the child has no replicas left, detach it from its parent and cascade the
        // delete to its entire subtree - orphaned children must never exist. Descendants
        // are removed after both bucket locks are released (see CascadeDeleteDescendants).
        if (replicas.empty())
        {
            vChildrenToCascade = std::move(it->second.m_vChildren);
            map.erase(it);
            RemoveChildLocked(m_vEntries[nParentBucket], strParentKey, strKey);
        }
        return true;
    };

    bool bResult;
    if (nChildBucket == nParentBucket)
    {
        std::lock_guard<std::mutex> lock(m_vMutexes[nChildBucket]);
        bResult = doUnregister();
    }
    else
    {
        const uint32_t nLo = std::min(nChildBucket, nParentBucket);
        const uint32_t nHi = std::max(nChildBucket, nParentBucket);
        std::lock_guard<std::mutex> lockLo(m_vMutexes[nLo]);
        std::lock_guard<std::mutex> lockHi(m_vMutexes[nHi]);
        bResult = doUnregister();
    }

    CascadeDeleteDescendants(std::move(vChildrenToCascade));
    return bResult;
}

//---------------------------------------------------------------------------------------------------------------------
size_t FlatRadixIndex::PurgeExpired()
{
    const uint64_t nNowMs = NowMs();
    size_t nTotalPurged = 0;

    // Parent-link cleanups and subtree cascades are deferred until the scanned bucket's
    // lock is released, so we never hold two bucket locks in non-min order (see
    // lock-ordering note at top).
    std::vector<std::pair<std::string, std::string>> vDeferredUnlinks; // {parentKey, childKey}
    std::vector<std::string> vDeferredCascade;                          // descendants to delete

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
                // Cascade the delete to this entry's subtree - orphaned children must
                // never exist. Detach from the parent (if any) as well.
                for (const auto& strChildKey : it->second.m_vChildren)
                {
                    vDeferredCascade.push_back(strChildKey);
                }
                if (!it->second.m_strParentKey.empty())
                {
                    vDeferredUnlinks.emplace_back(it->second.m_strParentKey, it->second.m_strKey);
                }
                it = map.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    for (const auto& [strParentKey, strChildKey] : vDeferredUnlinks)
    {
        RemoveChildLinkSingleLock(strParentKey, strChildKey);
    }

    CascadeDeleteDescendants(std::move(vDeferredCascade));

    return nTotalPurged;
}

//---------------------------------------------------------------------------------------------------------------------
PrefixLookupResult FlatRadixIndex::PrefixLookup(const std::string& strBaseParentKey,
                                                std::span<const std::string> vStorageKeys,
                                                uint64_t nNowMs) const
{
    PrefixLookupResult result;

    // The parent of the first block is strBaseParentKey; thereafter each block's parent is
    // the previous block's storage_key (a chain is parent-linked by construction).
    const std::string* pExpectedParent = &strBaseParentKey;

    size_t i = 0;
    for (; i < vStorageKeys.size(); ++i)
    {
        const std::string& strKey = vStorageKeys[i];
        auto entry = Lookup(strKey);

        // Stop at the first absent block or parent mismatch (broken chain).
        if (!entry.has_value() || entry->m_strParentKey != *pExpectedParent)
        {
            break;
        }

        // Select this block's replica, preferring a warm cache replica and falling back to the
        // durable blob replica (see LocatorEntry::SelectLiveReplica). A block with no live replica
        // of either tier breaks the usable prefix just like an absent block: the chain must be
        // contiguous, so stop here and report the rest missing. The selected replica keeps its
        // tier, which the RPC handler maps to node_replica (Cache) or blob_ref (Blob).
        const ContentReplica* pSelected = entry->SelectLiveReplica(nNowMs);
        if (pSelected == nullptr)
        {
            break;
        }

        result.selected.push_back(*pSelected);
        ++result.matched_count;

        // Advance the cursor: the next block's parent is this block's storage_key.
        pExpectedParent = &strKey;
    }

    return result;
}

//---------------------------------------------------------------------------------------------------------------------
void FlatRadixIndex::ForEachEntry(const std::function<void(const LocatorEntry&)>& fn) const
{
    for (uint32_t i = 0; i < NUM_LOCATOR_BUCKETS; ++i)
    {
        std::lock_guard<std::mutex> lock(m_vMutexes[i]);
        for (const auto& item : m_vEntries[i])
        {
            fn(item.second);
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
size_t FlatRadixIndex::GetEntryCount() const
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
size_t FlatRadixIndex::GetReplicaCount() const
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
void FlatRadixIndex::RemoveChildLinkSingleLock(const std::string& strParentKey, const std::string& strChildKey)
{
    const uint32_t nParentBucket = GetBucketIndex(strParentKey);
    std::lock_guard<std::mutex> lock(m_vMutexes[nParentBucket]);
    RemoveChildLocked(m_vEntries[nParentBucket], strParentKey, strChildKey);
}

//---------------------------------------------------------------------------------------------------------------------
void FlatRadixIndex::CascadeDeleteDescendants(std::vector<std::string> vChildKeys)
{
    // Iterative BFS/DFS over the subtree. Only one bucket lock is held at a time: each node
    // is erased under its own bucket lock, and its children are enqueued for the same
    // treatment. No parent-link fix-up is needed - every ancestor in the cascade is being
    // deleted too - so a single lock per node suffices and cannot deadlock.
    while (!vChildKeys.empty())
    {
        const std::string strKey = std::move(vChildKeys.back());
        vChildKeys.pop_back();

        const uint32_t nBucket = GetBucketIndex(strKey);
        std::lock_guard<std::mutex> lock(m_vMutexes[nBucket]);

        auto& map = m_vEntries[nBucket];
        auto it = map.find(strKey);
        if (it == map.end())
        {
            continue;
        }

        // Enqueue this node's children before erasing it, then drop the node entirely
        // regardless of any replicas it still holds.
        for (const auto& strChildKey : it->second.m_vChildren)
        {
            vChildKeys.push_back(strChildKey);
        }
        map.erase(it);
    }
}

//---------------------------------------------------------------------------------------------------------------------
uint32_t FlatRadixIndex::GetBucketIndex(const std::string& strKey) const
{
    return static_cast<uint32_t>(std::hash<std::string>{}(strKey) % NUM_LOCATOR_BUCKETS);
}

//---------------------------------------------------------------------------------------------------------------------
uint64_t FlatRadixIndex::NowMs()
{
    // Must match the epoch used to STAMP replica expiry (KVClient Impl::NowMs) and the epoch the
    // cache server uses to CHECK it. Replica expiry is a cross-host absolute timestamp, so this
    // uses system_clock (shared UNIX epoch), not steady_clock (per-host, non-comparable).
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

} // namespace kvclient
