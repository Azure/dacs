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
#ifndef ILOCATORINDEX_H_
#define ILOCATORINDEX_H_
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "LocatorEntry.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------

namespace kvclient
{

//---------------------------------------------------------------------------------------------------------------------
// PrefixLookupResult - outcome of walking a block chain against the index.
//
// The chain is matched in order; matching stops at the first block that is either
// absent, whose recorded parent_hash disagrees with the requested key, or that has no
// live (non-expired) replica. matched_count is the length of the usable prefix.
//
// NOTE: Only a single selected replica is returned per matched block. If clients ever need to
// choose among replicas (locality-aware selection, failover/retries), return all live replicas
// per block here (e.g. std::vector<std::vector<ContentReplica>>) and widen the RPC response
// (LocatorPrefixEntry) to carry the full set rather than one node.
//---------------------------------------------------------------------------------------------------------------------
struct PrefixLookupResult
{
    size_t matched_count = 0;                  // Number of leading blocks that matched in order
    std::vector<ContentReplica> selected;      // selected[i] = the live replica chosen for the i-th matched block
};

//---------------------------------------------------------------------------------------------------------------------
// ILocatorIndex - storage-strategy abstraction behind LocatorCache.
//
// Implementations own the concurrent entry storage and the prefix-tree bookkeeping.
// LocatorCache delegates all entry operations here, which lets the backend be swapped
// (e.g. a flat map with children today, a true trie later) without touching callers.
//---------------------------------------------------------------------------------------------------------------------
class ILocatorIndex
{
  public:
    virtual ~ILocatorIndex() = default;

    // Register a content replica for a key, recording its place in the prefix tree.
    // Parent/root are storage_keys ("" when the block is a root with no parent).
    virtual void Register(const std::string& strKey, const std::string& strParentKey,
                          const std::string& strRootKey, const ContentReplica& replica) = 0;

    // Lookup all replicas for a key. Returns nullopt if key not found.
    virtual std::optional<LocatorEntry> Lookup(const std::string& strKey) const = 0;

    // Refresh TTL for a specific replica on a node.
    virtual bool Refresh(const std::string& strKey, uint32_t nNodeId, uint64_t nNewExpiresAt) = 0;

    // Unregister a specific replica (e.g., on eviction).
    virtual bool Unregister(const std::string& strKey, uint32_t nNodeId) = 0;

    // Remove all expired replicas across all entries. Returns total replicas removed.
    virtual size_t PurgeExpired() = 0;

    // Walk a block chain in order, returning the longest matching prefix. The chain is the
    // ordered storage keys; each block's parent is the previous key in the span, with
    // strBaseParentKey as the parent of the first block ("" for a root chain). nNowMs is the
    // current time (epoch ms) used to decide replica liveness; a block with no non-expired
    // replica truncates the prefix exactly like an absent block.
    virtual PrefixLookupResult PrefixLookup(const std::string& strBaseParentKey,
                                            std::span<const std::string> vStorageKeys,
                                            uint64_t nNowMs) const = 0;

    // Counts.
    virtual size_t GetEntryCount() const = 0;
    virtual size_t GetReplicaCount() const = 0;
};

} // namespace kvclient

//---------------------------------------------------------------------------------------------------------------------
#endif // ILOCATORINDEX_H_
