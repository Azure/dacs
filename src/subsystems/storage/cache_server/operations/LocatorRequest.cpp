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
#include "LocatorRequest.h"
#include "ServerLocatorCache.h"
#include "ConnectionUtils.h"
#include "Logger.h"
#include "Metrics.h"

#include <chrono>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;
using namespace kvclient;
//---------------------------------------------------------------------------------------------------------------------
namespace
{
    // Build an in-memory ContentReplica from its protobuf form, preserving the tier. tier is an
    // additive proto field defaulting to 0 (Cache), so registrations from older clients that never
    // set it are treated as cache replicas exactly as before.
    ContentReplica FromProto(const LocatorContentReplica& proto)
    {
        ContentReplica replica(proto.nodeid(), proto.expiresat(), proto.lastrefreshed(), proto.contentsize());
        replica.m_tier = static_cast<ReplicaTier>(proto.tier());
        return replica;
    }
} // namespace
//---------------------------------------------------------------------------------------------------------------------

bool HandleLocatorRegister(IConnection& clientConnection, const LocatorRegisterRequest& request)
{
    const std::string& requestId = request.requestid();
    LocatorRegisterResponse response;
    response.set_requestid(requestId);

    LOG_DEBUG_REQID_STR("LocatorRegister key={} node={}", requestId, request.key(), request.replica().nodeid());

    ContentReplica replica = FromProto(request.replica());

    GetServerLocatorCache().Register(request.key(), request.parenthash(), request.roothash(), replica);
    Metrics::IncrementLocatorRegistrations();
    Metrics::SetLocatorEntryCount(GetServerLocatorCache().GetEntryCount());
    Metrics::SetLocatorReplicaCount(GetServerLocatorCache().GetReplicaCount());

    response.set_result(LocatorRegisterResponse::SUCCESS);
    return SendMessageToRemoteFinal(clientConnection, response, requestId);
}

//---------------------------------------------------------------------------------------------------------------------
bool HandleLocatorLookup(IConnection& clientConnection, const LocatorLookupRequest& request)
{
    const std::string& requestId = request.requestid();
    LocatorLookupResponse response;
    response.set_requestid(requestId);

    LOG_DEBUG_REQID_STR("LocatorLookup key={}", requestId, request.key());

    auto entry = GetServerLocatorCache().Lookup(request.key());
    Metrics::IncrementLocatorLookups(entry.has_value());

    if (!entry.has_value())
    {
        response.set_result(LocatorLookupResponse::NOT_FOUND);
    }
    else
    {
        response.set_result(LocatorLookupResponse::SUCCESS);
        for (const auto& replica : entry->m_vReplicas)
        {
            auto* pReplica = response.add_replicas();
            pReplica->set_nodeid(replica.m_nNodeId);
            pReplica->set_expiresat(replica.m_nExpiresAt);
            pReplica->set_lastrefreshed(replica.m_nLastRefreshed);
            pReplica->set_contentsize(replica.m_nContentSize);
            pReplica->set_tier(static_cast<uint32_t>(replica.m_tier));
        }
    }

    return SendMessageToRemoteFinal(clientConnection, response, requestId);
}

//---------------------------------------------------------------------------------------------------------------------
bool HandleLocatorRefresh(IConnection& clientConnection, const LocatorRefreshRequest& request)
{
    const std::string& requestId = request.requestid();
    LocatorRefreshResponse response;
    response.set_requestid(requestId);

    LOG_DEBUG_REQID_STR("LocatorRefresh key={} node={}", requestId, request.key(), request.nodeid());

    bool bRefreshed = GetServerLocatorCache().Refresh(
        request.key(), request.nodeid(), request.newexpiresat());
    if (bRefreshed)
    {
        Metrics::IncrementTTLExtensions();
    }

    response.set_result(bRefreshed
        ? LocatorRefreshResponse::SUCCESS
        : LocatorRefreshResponse::NOT_FOUND);

    return SendMessageToRemoteFinal(clientConnection, response, requestId);
}

//---------------------------------------------------------------------------------------------------------------------
bool HandleLocatorUnregister(IConnection& clientConnection, const LocatorUnregisterRequest& request)
{
    const std::string& requestId = request.requestid();
    LocatorUnregisterResponse response;
    response.set_requestid(requestId);

    LOG_DEBUG_REQID_STR("LocatorUnregister key={} node={}", requestId, request.key(), request.nodeid());

    bool bUnregistered = GetServerLocatorCache().Unregister(request.key(), request.nodeid());
    if (bUnregistered)
    {
        Metrics::SetLocatorEntryCount(GetServerLocatorCache().GetEntryCount());
        Metrics::SetLocatorReplicaCount(GetServerLocatorCache().GetReplicaCount());
    }

    response.set_result(bUnregistered
        ? LocatorUnregisterResponse::SUCCESS
        : LocatorUnregisterResponse::NOT_FOUND);

    return SendMessageToRemoteFinal(clientConnection, response, requestId);
}

//---------------------------------------------------------------------------------------------------------------------
bool HandleLocatorPrefixLookup(IConnection& clientConnection,
                               const LocatorPrefixLookupRequest& request)
{
    const std::string& requestId = request.requestid();
    LocatorPrefixLookupResponse response;
    response.set_requestid(requestId);

    LOG_DEBUG_REQID_STR("LocatorPrefixLookup keys_size={}", requestId, request.keys_size());

    // The request carries storage_key() strings in chain order, along with the storage_key of
    // the parent of the first block (empty when the chain roots at the caller).
    std::vector<std::string> vStorageKeys;
    vStorageKeys.reserve(static_cast<std::size_t>(request.keys_size()));
    for (const auto& strKey : request.keys())
    {
        vStorageKeys.push_back(strKey);
    }

    // Replica expiry (m_nExpiresAt) is stamped by the CLIENT on another host and shipped here, so
    // it must be compared against the SAME shared epoch. Use system_clock (UNIX epoch, NTP-synced);
    // steady_clock is per-host (time since boot) and would make fresh replicas look expired/live at
    // random depending on the relative uptimes of the client and server nodes.
    uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    auto prefixResult = GetServerLocatorCache().PrefixLookup(request.baseparenthash(), vStorageKeys, nowMs);

    response.set_result(LocatorPrefixLookupResponse::SUCCESS);

    // The walk truncated the chain at the first missing/expired block and chose one live replica
    // per matched block, so this is a pure projection with no replica re-scan. A cache-tier replica
    // is reported as node_replica (route to that node); the durable blob replica is reported as
    // blob_ref, an existence marker whose location the client derives from the key.
    for (const auto& replica : prefixResult.selected)
    {
        auto* pOutEntry = response.add_matched();
        pOutEntry->set_content_size(replica.m_nContentSize);
        if (replica.m_tier == ReplicaTier::Blob)
        {
            pOutEntry->mutable_blob_ref();   // existence marker; blob_path stays empty (derived client-side)
        }
        else
        {
            pOutEntry->mutable_node_replica()->set_node_id(replica.m_nNodeId);
        }
    }

    response.set_matched_count(static_cast<uint32_t>(response.matched_size()));

    return SendMessageToRemoteFinal(clientConnection, response, requestId);
}

//---------------------------------------------------------------------------------------------------------------------
bool HandleLocatorRegisterChain(IConnection& clientConnection,
                                const LocatorRegisterChainRequest& request)
{
    const std::string& requestId = request.requestid();
    LocatorRegisterChainResponse response;
    response.set_requestid(requestId);

    LOG_DEBUG_REQID_STR("LocatorRegisterChain entries_size={}", requestId, request.entries_size());

    // Register every block of the chain, recording each block's place in the prefix tree.
    for (const auto& entry : request.entries())
    {
        ContentReplica replica = FromProto(entry.replica());

        GetServerLocatorCache().Register(entry.key(), entry.parenthash(), entry.roothash(), replica);
        Metrics::IncrementLocatorRegistrations();
    }
    Metrics::SetLocatorEntryCount(GetServerLocatorCache().GetEntryCount());
    Metrics::SetLocatorReplicaCount(GetServerLocatorCache().GetReplicaCount());

    response.set_result(LocatorRegisterChainResponse::SUCCESS);
    return SendMessageToRemoteFinal(clientConnection, response, requestId);
}
