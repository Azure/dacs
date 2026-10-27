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
// Tests that the server locator index survives a restart: its entries are written to the snapshot
// with their parent and root keys and restored, so chain prefix lookups match again afterwards.

#include <gtest/gtest.h>
#include "FlatMapIndex.h"
#include "FlatRadixIndex.h"
#include "ServerLocatorCache.h"
#include "snapshot.pb.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

using namespace kvclient;

namespace
{

constexpr uint64_t kHourMs = 3600ULL * 1000;

uint64_t EpochNowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string Key(size_t i) { return "b:" + std::to_string(1000 + i); }

std::vector<std::string> ChainKeys(size_t n)
{
    std::vector<std::string> keys;
    for (size_t i = 0; i < n; ++i)
    {
        keys.push_back(Key(i));
    }
    return keys;
}

// Register keys[0..n) as one chain: keys[0] is the root, every later block names its predecessor
// as parent and keys[0] as root, exactly as LocatorRegisterChain does.
void RegisterChain(LocatorCache& cache, const std::vector<std::string>& keys,
                   const ContentReplica& replica)
{
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const std::string parent = i == 0 ? std::string{} : keys[i - 1];
        const std::string root = i == 0 ? std::string{} : keys[0];
        cache.Register(keys[i], parent, root, replica);
    }
}

// Write the server locator cache into a snapshot, serialize and parse it as a restart would, then
// restore it into a fresh index of the same kind.
template <typename Index>
size_t SaveAndRestart(uint64_t nNowMs)
{
    snapshot::Snapshot saved;
    AddLocatorEntriesToSnapshot(saved);
    std::string bytes;
    EXPECT_TRUE(saved.SerializeToString(&bytes));

    snapshot::Snapshot loaded;
    EXPECT_TRUE(loaded.ParseFromString(bytes));
    ResetServerLocatorCacheForTest(std::make_unique<Index>(), /*nNodeId=*/0, /*nNumNodes=*/1);
    return RestoreLocatorEntriesFromSnapshot(loaded, nNowMs);
}

LocatorEntry Entry(const std::string& key, const std::string& parent, const std::string& root,
                   const ContentReplica& replica)
{
    LocatorEntry entry(key, 0, parent, root);
    entry.m_vReplicas.push_back(replica);
    return entry;
}

} // namespace

//---------------------------------------------------------------------------------------------------------------------
// The radix index loses every chain on a restart today: this is the regression test.
TEST(LocatorSnapshotTest, RadixChainsMatchAgainAfterRestart)
{
    const uint64_t nNowMs = EpochNowMs();
    ResetServerLocatorCacheForTest(std::make_unique<FlatRadixIndex>(), 0, 1);
    const auto keys = ChainKeys(8);
    RegisterChain(GetServerLocatorCache(), keys, ContentReplica(0, nNowMs + kHourMs, nNowMs, 4096));

    EXPECT_EQ(SaveAndRestart<FlatRadixIndex>(nNowMs), keys.size());

    auto& cache = GetServerLocatorCache();
    EXPECT_EQ(cache.GetEntryCount(), keys.size());
    EXPECT_EQ(cache.PrefixLookup("", keys, nNowMs).matched_count, keys.size());
    // A lookup that continues a chain from a block already on the GPU also matches.
    std::vector<std::string> tail(keys.begin() + 3, keys.end());
    EXPECT_EQ(cache.PrefixLookup(keys[2], tail, nNowMs).matched_count, tail.size());
}

//---------------------------------------------------------------------------------------------------------------------
TEST(LocatorSnapshotTest, ReplicaFieldsSurviveTheRoundTrip)
{
    const uint64_t nNowMs = EpochNowMs();
    ResetServerLocatorCacheForTest(std::make_unique<FlatRadixIndex>(), 0, 2);
    auto& cache = GetServerLocatorCache();
    cache.Register(Key(0), "", "", ContentReplica(1, nNowMs + kHourMs, nNowMs - 5, 917568));
    cache.Register(Key(0), "", "",
                   ContentReplica::MakeBlobReplica(nNowMs + 24 * kHourMs, nNowMs - 7, 917568));

    EXPECT_EQ(SaveAndRestart<FlatRadixIndex>(nNowMs), 1u);

    auto entry = GetServerLocatorCache().Lookup(Key(0));
    ASSERT_TRUE(entry.has_value());
    ASSERT_EQ(entry->m_vReplicas.size(), 2u);
    auto cacheReplica = std::find_if(entry->m_vReplicas.begin(), entry->m_vReplicas.end(),
        [](const ContentReplica& r) { return r.m_tier == ReplicaTier::Cache; });
    auto blobReplica = std::find_if(entry->m_vReplicas.begin(), entry->m_vReplicas.end(),
        [](const ContentReplica& r) { return r.m_tier == ReplicaTier::Blob; });
    ASSERT_NE(cacheReplica, entry->m_vReplicas.end());
    ASSERT_NE(blobReplica, entry->m_vReplicas.end());
    EXPECT_EQ(cacheReplica->m_nNodeId, 1u);
    EXPECT_EQ(cacheReplica->m_nExpiresAt, nNowMs + kHourMs);
    EXPECT_EQ(cacheReplica->m_nLastRefreshed, nNowMs - 5);
    EXPECT_EQ(cacheReplica->m_nContentSize, 917568u);
    EXPECT_EQ(blobReplica->m_nExpiresAt, nNowMs + 24 * kHourMs);
}

//---------------------------------------------------------------------------------------------------------------------
TEST(LocatorSnapshotTest, ExpiredReplicasAndEntriesAreNotRestored)
{
    const uint64_t nNowMs = EpochNowMs();
    ResetServerLocatorCacheForTest(std::make_unique<FlatRadixIndex>(), 0, 1);
    auto& cache = GetServerLocatorCache();
    cache.Register(Key(0), "", "", ContentReplica(0, nNowMs + kHourMs, nNowMs, 1));
    cache.Register(Key(0), "", "", ContentReplica(1, nNowMs - 1, nNowMs, 1));  // expired
    cache.Register(Key(1), "", "", ContentReplica(0, nNowMs - 1, nNowMs, 1));  // expired

    EXPECT_EQ(SaveAndRestart<FlatRadixIndex>(nNowMs), 1u);

    auto entry = GetServerLocatorCache().Lookup(Key(0));
    ASSERT_TRUE(entry.has_value());
    ASSERT_EQ(entry->m_vReplicas.size(), 1u);
    EXPECT_EQ(entry->m_vReplicas[0].m_nNodeId, 0u);
    EXPECT_FALSE(GetServerLocatorCache().Lookup(Key(1)).has_value());
}

//---------------------------------------------------------------------------------------------------------------------
// Entries arrive in an arbitrary order (the index is hashed). Restoring children before their
// parent would leave the children unlinked, and cascade deletion would then leak them.
TEST(LocatorSnapshotTest, RestoreRelinksChildrenSoCascadeDeletionStillWorks)
{
    const uint64_t nNowMs = EpochNowMs();
    ResetServerLocatorCacheForTest(std::make_unique<FlatRadixIndex>(), 0, 1);
    const auto keys = ChainKeys(5);
    const ContentReplica replica(0, nNowMs + kHourMs, nNowMs, 1);
    std::vector<LocatorEntry> entries;
    for (size_t i = keys.size(); i-- > 0;)  // children first
    {
        entries.push_back(Entry(keys[i], i == 0 ? "" : keys[i - 1], i == 0 ? "" : keys[0], replica));
    }

    EXPECT_EQ(GetServerLocatorCache().Restore(std::move(entries), nNowMs), keys.size());
    EXPECT_EQ(GetServerLocatorCache().PrefixLookup("", keys, nNowMs).matched_count, keys.size());

    // Removing the root's only replica removes the whole chain through the child links.
    EXPECT_TRUE(GetServerLocatorCache().Unregister(keys[0], 0));
    EXPECT_EQ(GetServerLocatorCache().GetEntryCount(), 0u);
}

//---------------------------------------------------------------------------------------------------------------------
// One 32K-token prompt in 16-token blocks is a chain 2,048 deep; restore must not recurse.
TEST(LocatorSnapshotTest, DeepChainsAreRestoredInFull)
{
    const uint64_t nNowMs = EpochNowMs();
    ResetServerLocatorCacheForTest(std::make_unique<FlatRadixIndex>(), 0, 1);
    const auto keys = ChainKeys(20000);
    RegisterChain(GetServerLocatorCache(), keys, ContentReplica(0, nNowMs + kHourMs, nNowMs, 1));

    EXPECT_EQ(SaveAndRestart<FlatRadixIndex>(nNowMs), keys.size());
    EXPECT_EQ(GetServerLocatorCache().PrefixLookup("", keys, nNowMs).matched_count, keys.size());
}

//---------------------------------------------------------------------------------------------------------------------
// A block whose parent entry expired is still useful to a lookup that starts after the parent,
// so it is restored even though its parent is not.
TEST(LocatorSnapshotTest, ChildOfAnExpiredParentIsStillRestored)
{
    const uint64_t nNowMs = EpochNowMs();
    ResetServerLocatorCacheForTest(std::make_unique<FlatRadixIndex>(), 0, 1);
    std::vector<LocatorEntry> entries{
        Entry(Key(0), "", "", ContentReplica(0, nNowMs - 1, nNowMs, 1)),  // expired root
        Entry(Key(1), Key(0), Key(0), ContentReplica(0, nNowMs + kHourMs, nNowMs, 1)),
    };

    EXPECT_EQ(GetServerLocatorCache().Restore(std::move(entries), nNowMs), 1u);
    const std::vector<std::string> tail{Key(1)};
    EXPECT_EQ(GetServerLocatorCache().PrefixLookup(Key(0), tail, nNowMs).matched_count, 1u);
}

//---------------------------------------------------------------------------------------------------------------------
TEST(LocatorSnapshotTest, FlatMapEntriesSurviveTheRoundTrip)
{
    const uint64_t nNowMs = EpochNowMs();
    ResetServerLocatorCacheForTest(std::make_unique<FlatMapIndex>(), 0, 1);
    GetServerLocatorCache().Register(Key(0), ContentReplica(0, nNowMs + kHourMs, nNowMs, 64));
    GetServerLocatorCache().Register(Key(1), ContentReplica(0, nNowMs + kHourMs, nNowMs, 64));

    EXPECT_EQ(SaveAndRestart<FlatMapIndex>(nNowMs), 2u);
    EXPECT_TRUE(GetServerLocatorCache().Lookup(Key(0)).has_value());
    EXPECT_TRUE(GetServerLocatorCache().Lookup(Key(1)).has_value());
}

//---------------------------------------------------------------------------------------------------------------------
TEST(LocatorSnapshotTest, ReplicaWithAnUnknownTierIsSkipped)
{
    const uint64_t nNowMs = EpochNowMs();
    snapshot::Snapshot loaded;
    auto* pEntry = loaded.add_locatorentries();
    pEntry->set_key(Key(0));
    auto* pReplica = pEntry->add_replicas();
    pReplica->set_tier(7);
    pReplica->set_expiresatms(nNowMs + kHourMs);
    ResetServerLocatorCacheForTest(std::make_unique<FlatRadixIndex>(), 0, 1);

    EXPECT_EQ(RestoreLocatorEntriesFromSnapshot(loaded, nNowMs), 0u);
    EXPECT_EQ(GetServerLocatorCache().GetEntryCount(), 0u);
}
