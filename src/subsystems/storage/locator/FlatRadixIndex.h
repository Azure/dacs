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
#ifndef FLATRADIXINDEX_H_
#define FLATRADIXINDEX_H_
#pragma once
//---------------------------------------------------------------------------------------------------------------------
#include "ILocatorIndex.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------

namespace kvclient
{

// Number of lock-striped buckets for concurrent access
static constexpr uint32_t NUM_LOCATOR_BUCKETS = 64;

//---------------------------------------------------------------------------------------------------------------------
// FlatRadixIndex - flat hash map with per-entry children.
//
// Entries live in a lock-striped array of unordered_maps keyed by storage_key. Each
// LocatorEntry additionally records its parent's, root's, and direct children's storage
// keys, so the prefix tree is recoverable without a separate node graph.
//
// Thread-safe via bucketed mutexes. Operations that touch both a child and its parent
// acquire two bucket locks; see FlatRadixIndex.cpp for the lock-ordering invariant.
//---------------------------------------------------------------------------------------------------------------------
class FlatRadixIndex final : public ILocatorIndex
{
  public:
    FlatRadixIndex();
    ~FlatRadixIndex() override = default;

    void Register(const std::string& strKey, const std::string& strParentKey,
                  const std::string& strRootKey, const ContentReplica& replica) override;
    std::optional<LocatorEntry> Lookup(const std::string& strKey) const override;
    bool Refresh(const std::string& strKey, uint32_t nNodeId, uint64_t nNewExpiresAt) override;
    bool Unregister(const std::string& strKey, uint32_t nNodeId) override;
    size_t PurgeExpired() override;
    PrefixLookupResult PrefixLookup(const std::string& strBaseParentKey,
                                    std::span<const std::string> vStorageKeys,
                                    uint64_t nNowMs) const override;
    void ForEachEntry(const std::function<void(const LocatorEntry&)>& fn) const override;
    size_t GetEntryCount() const override;
    size_t GetReplicaCount() const override;

  private:
    uint32_t GetBucketIndex(const std::string& strKey) const;
    static uint64_t NowMs();

    // Remove a child's storage_key from its parent entry using only the parent bucket's
    // lock. Used for deferred PurgeExpired cleanup, where the child entry is already gone
    // so no second entry is mutated and a single lock cannot deadlock.
    void RemoveChildLinkSingleLock(const std::string& strParentKey, const std::string& strChildKey);

    // Recursively delete every descendant reachable from the given child keys. Deleting a
    // node cascades to its whole subtree so orphaned children can never outlive their
    // parent. Each entry is erased under its own bucket lock only; no parent-link fix-up is
    // needed because every ancestor in the cascade is being deleted too, so a single lock
    // per node cannot deadlock. Descendants are removed regardless of remaining replicas.
    void CascadeDeleteDescendants(std::vector<std::string> vChildKeys);

    std::vector<std::unordered_map<std::string, LocatorEntry>> m_vEntries;
    mutable std::vector<std::mutex> m_vMutexes;
};

} // namespace kvclient

//---------------------------------------------------------------------------------------------------------------------
#endif // FLATRADIXINDEX_H_
