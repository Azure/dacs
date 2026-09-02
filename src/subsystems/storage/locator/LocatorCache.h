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
#ifndef LOCATORCACHE_H_
#define LOCATORCACHE_H_
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "LocatorEntry.h"
#include "ConsistentHash.h"
#include "ILocatorIndex.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
//---------------------------------------------------------------------------------------------------------------------

namespace kvclient
{

//---------------------------------------------------------------------------------------------------------------------
// LocatorCache - in-memory distributed hash table for KV location metadata.
//
// Each cache server hosts a LocatorCache instance. Keys are assigned to locator nodes
// via consistent hashing (JumpConsistentHash). Clients send Register/Lookup/Refresh/Unregister
// RPCs to the appropriate locator node.
//
// LocatorCache is a thin routing/ownership wrapper: it owns this node's identity and an
// ILocatorIndex backend (FlatMapIndex by default) that holds the entry storage and the
// prefix-tree bookkeeping. The backend is injectable to allow experimenting with other
// strategies (e.g. a true trie).
//---------------------------------------------------------------------------------------------------------------------
class LocatorCache
{
  public:
    // Construct with an optional index backend. When pIndex is null, a FlatMapIndex is
    // default-constructed. This is the injection point for alternative backends.
    explicit LocatorCache(std::unique_ptr<ILocatorIndex> pIndex = nullptr);
    ~LocatorCache() = default;

    // Initialise with this node's ID and total node count for consistent hashing
    void Init(uint32_t nNodeId, uint32_t nNumNodes);

    //-- Core operations (called by RPC handlers) --

    // Register a content replica for a key
    void Register(const std::string& strKey, const ContentReplica& replica);

    // Register a content replica for a key, recording its place in the prefix tree.
    // Parent/root are storage_keys ("" when the block is a root with no parent).
    void Register(const std::string& strKey, const std::string& strParentKey,
                  const std::string& strRootKey, const ContentReplica& replica);

    // Lookup all replicas for a key. Returns nullopt if key not found.
    std::optional<LocatorEntry> Lookup(const std::string& strKey) const;

    // Refresh TTL for a specific replica on a node
    bool Refresh(const std::string& strKey, uint32_t nNodeId, uint64_t nNewExpiresAt);

    // Unregister a specific replica (e.g., on eviction)
    bool Unregister(const std::string& strKey, uint32_t nNodeId);

    // Walk a block chain in order, returning the longest matching prefix. nNowMs (epoch ms)
    // determines replica liveness; blocks with no live replica truncate the prefix.
    PrefixLookupResult PrefixLookup(const std::string& strBaseParentKey,
                                    std::span<const std::string> vStorageKeys,
                                    uint64_t nNowMs) const;

    //-- Maintenance --

    // Remove all expired replicas across all entries. Returns total entries removed.
    size_t PurgeExpired();

    //-- Accessors --

    uint32_t GetNodeId() const { return m_nNodeId; }
    uint32_t GetNumNodes() const { return m_nNumNodes; }
    size_t GetEntryCount() const;
    size_t GetReplicaCount() const;

  private:
    uint32_t m_nNodeId;
    uint32_t m_nNumNodes;

    // Pluggable storage backend (FlatMapIndex by default).
    std::unique_ptr<ILocatorIndex> m_pIndex;
};

} // namespace kvclient

//---------------------------------------------------------------------------------------------------------------------
#endif // LOCATORCACHE_H_
