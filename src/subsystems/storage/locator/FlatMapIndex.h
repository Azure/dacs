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
#ifndef FLATMAPINDEX_H_
#define FLATMAPINDEX_H_
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

//---------------------------------------------------------------------------------------------------------------------
// FlatMapIndex - the original, tree-agnostic flat locator storage.
//
// A lock-striped array of
// unordered_maps keyed by storage_key, with a single bucket lock guarding each operation.
// It records the parent/root storage keys carried on each registration, but it deliberately
// keeps NO child links and does NOT model chains: because each block is distributed
// independently by consistent_hash(block_key), a single node only holds a scattered subset of
// a chain, so PrefixLookup is unsupported here (it asserts in debug and reports a full miss in
// release).
//---------------------------------------------------------------------------------------------------------------------
class FlatMapIndex final : public ILocatorIndex
{
  public:
    FlatMapIndex();
    ~FlatMapIndex() override = default;

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

    std::vector<std::unordered_map<std::string, LocatorEntry>> m_vEntries;
    mutable std::vector<std::mutex> m_vMutexes;
};

} // namespace kvclient

//---------------------------------------------------------------------------------------------------------------------
#endif // FLATMAPINDEX_H_
