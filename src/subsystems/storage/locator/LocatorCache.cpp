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
#include "LocatorCache.h"
#include "FlatMapIndex.h"
#include "FlatRadixIndex.h"
#include "LoggingApi.h"

#include <utility>
//---------------------------------------------------------------------------------------------------------------------

namespace kvclient
{

//---------------------------------------------------------------------------------------------------------------------
LocatorCache::LocatorCache(std::unique_ptr<ILocatorIndex> pIndex)
    : m_nNodeId(0)
    , m_nNumNodes(0)
    , m_pIndex(pIndex ? std::move(pIndex) : std::make_unique<FlatMapIndex>())
    // , m_pIndex(pIndex ? std::move(pIndex) : std::make_unique<FlatRadixIndex>())
{
}

//---------------------------------------------------------------------------------------------------------------------
void LocatorCache::Init(uint32_t nNodeId, uint32_t nNumNodes)
{
    m_nNodeId = nNodeId;
    m_nNumNodes = nNumNodes;
}

//---------------------------------------------------------------------------------------------------------------------
void LocatorCache::Register(const std::string& strKey, const ContentReplica& replica)
{
    // Convenience overload for the legacy single-block path: no prefix-tree position.
    m_pIndex->Register(strKey, /*strParentKey=*/std::string{}, /*strRootKey=*/std::string{}, replica);
}

//---------------------------------------------------------------------------------------------------------------------
void LocatorCache::Register(const std::string& strKey, const std::string& strParentKey,
                            const std::string& strRootKey, const ContentReplica& replica)
{
#ifndef NDEBUG
    // Co-location invariant (debug-only): a chain descendant - one that names an explicit root -
    // is registered on the node its ROOT hashes to, because the client routes a whole chain by
    // consistent_hash(root). If this fires, chain routing has been mixed with per-block routing
    // for the same key. Only descendants (non-empty root) are checked: a chain root and the legacy
    // single-key path route by their own key, and post-restart re-registration uses the legacy
    // overload, so checking the empty-root case would raise false positives.
    if (!strRootKey.empty() && m_nNumNodes > 0)
    {
        const uint32_t nRootNode = GetLocatorNode(strRootKey, m_nNumNodes);
        if (nRootNode != m_nNodeId)
        {
            LOG_WARN("LocatorCache: co-location violation - block {} (root {}) registered on node "
                     "{} but its root hashes to node {}; chain and per-block routing may be mixed",
                     strKey, strRootKey, m_nNodeId, nRootNode);
        }
    }
#endif

    m_pIndex->Register(strKey, strParentKey, strRootKey, replica);
}

//---------------------------------------------------------------------------------------------------------------------
std::optional<LocatorEntry> LocatorCache::Lookup(const std::string& strKey) const
{
    return m_pIndex->Lookup(strKey);
}

//---------------------------------------------------------------------------------------------------------------------
bool LocatorCache::Refresh(const std::string& strKey, uint32_t nNodeId, uint64_t nNewExpiresAt)
{
    return m_pIndex->Refresh(strKey, nNodeId, nNewExpiresAt);
}

//---------------------------------------------------------------------------------------------------------------------
bool LocatorCache::Unregister(const std::string& strKey, uint32_t nNodeId)
{
    return m_pIndex->Unregister(strKey, nNodeId);
}

//---------------------------------------------------------------------------------------------------------------------
PrefixLookupResult LocatorCache::PrefixLookup(const std::string& strBaseParentKey,
                                              std::span<const std::string> vStorageKeys,
                                              uint64_t nNowMs) const
{
    return m_pIndex->PrefixLookup(strBaseParentKey, vStorageKeys, nNowMs);
}

//---------------------------------------------------------------------------------------------------------------------
size_t LocatorCache::PurgeExpired()
{
    return m_pIndex->PurgeExpired();
}

//---------------------------------------------------------------------------------------------------------------------
size_t LocatorCache::GetEntryCount() const
{
    return m_pIndex->GetEntryCount();
}

//---------------------------------------------------------------------------------------------------------------------
size_t LocatorCache::GetReplicaCount() const
{
    return m_pIndex->GetReplicaCount();
}

} // namespace kvclient
