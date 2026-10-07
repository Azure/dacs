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
// Server-handler tests for locator RPCs. These exercise the request handlers over a
// capturing IConnection (no sockets) and verify both the serialized response and the resulting
// state in the server locator cache.
//
// The register-path tests run against the default flat backend. The prefix-lookup tests install a
// FlatRadixIndex backend via ResetServerLocatorCacheForTest, since the flat backend does not
// support chain walks (its PrefixLookup asserts in debug builds).
//---------------------------------------------------------------------------------------------------------------------
#include <gtest/gtest.h>

#include "LocatorRequest.h"
#include "ServerLocatorCache.h"
#include "FlatMapIndex.h"
#include "FlatRadixIndex.h"
#include "IConnection.h"
#include "Constants.h"
#include "cache.pb.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>
//---------------------------------------------------------------------------------------------------------------------
using namespace cache;

namespace
{

uint64_t EpochNowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// Minimal IConnection that captures everything written by the handler so the framed response can
// be decoded back into a protobuf message.
class CapturingConnection : public IConnection
{
  public:
    int Read(char*, int) override { return 0; }

    int Write(char* buffer, int size) override
    {
        m_written.append(buffer, static_cast<size_t>(size));
        return size;
    }

    int WriteFinal(char* buffer, int size) override
    {
        m_written.append(buffer, static_cast<size_t>(size));
        return size;
    }

    bool Sendfile(int, uint64_t, uint64_t) override { return true; }
    bool Recvfile(int, uint64_t) override { return true; }
    int GetId() override { return 4242; }

    // Decode the captured framed response (4-byte length prefix + serialized proto).
    template <typename TMessage>
    bool DecodeResponse(TMessage& message) const
    {
        if (m_written.size() < static_cast<size_t>(REQUEST_METADATA_SIZE))
        {
            return false;
        }
        return message.ParseFromArray(m_written.data() + REQUEST_METADATA_SIZE,
                                      static_cast<int>(m_written.size() - REQUEST_METADATA_SIZE));
    }

  private:
    std::string m_written;
};

LocatorContentReplica MakeReplica(uint32_t nNodeId, uint64_t nContentSize)
{
    const uint64_t nNowMs = EpochNowMs();
    LocatorContentReplica replica;
    replica.set_nodeid(nNodeId);
    replica.set_expiresat(nNowMs + 60'000);
    replica.set_lastrefreshed(nNowMs);
    replica.set_contentsize(nContentSize);
    return replica;
}

} // namespace

//---------------------------------------------------------------------------------------------------------------------
class LocatorRequestTest : public ::testing::Test
{
  protected:
    // Start each test from a fresh flat-backed cache so state never leaks between tests. Prefix
    // tests override the backend with UseRadixBackend().
    void SetUp() override
    {
        kvclient::ResetServerLocatorCacheForTest(
            std::make_unique<kvclient::FlatMapIndex>(), /*nNodeId=*/0, /*nNumNodes=*/3);
    }

    static void UseRadixBackend()
    {
        kvclient::ResetServerLocatorCacheForTest(
            std::make_unique<kvclient::FlatRadixIndex>(), /*nNodeId=*/0, /*nNumNodes=*/3);
    }

    // Register a single block directly into the server cache (bypassing the RPC handler) so a
    // prefix-lookup test can set up chain state.
    static void RegisterBlock(const std::string& strKey, const std::string& strParent,
                              const std::string& strRoot, uint32_t nNodeId, uint64_t nContentSize,
                              uint64_t nExpiresAt = 0)
    {
        const uint64_t nNowMs = EpochNowMs();
        if (nExpiresAt == 0)
        {
            nExpiresAt = nNowMs + 60'000;
        }
        ContentReplica replica(nNodeId, nExpiresAt, nNowMs, nContentSize);
        kvclient::GetServerLocatorCache().Register(strKey, strParent, strRoot, replica);
    }
};

TEST_F(LocatorRequestTest, EveryHandlerEchoesTheRequestId)
{
    UseRadixBackend();
    for (const std::string id : {"request-1", "request-2", ""})
    {
        SCOPED_TRACE(id);
        const std::string key = "correlation-" + id;
        LocatorRegisterRequest reg;
        reg.set_requestid(id);
        reg.set_key(key);
        *reg.mutable_replica() = MakeReplica(1, 100);
        CapturingConnection registerConn;
        ASSERT_TRUE(HandleLocatorRegister(registerConn, reg));
        LocatorRegisterResponse registered;
        ASSERT_TRUE(registerConn.DecodeResponse(registered));
        EXPECT_EQ(registered.requestid(), id);
        EXPECT_EQ(registered.result(), LocatorRegisterResponse::SUCCESS);

        LocatorRegisterChainRequest chain;
        chain.set_requestid(id);
        CapturingConnection chainConn;
        ASSERT_TRUE(HandleLocatorRegisterChain(chainConn, chain));
        LocatorRegisterChainResponse chained;
        ASSERT_TRUE(chainConn.DecodeResponse(chained));
        EXPECT_EQ(chained.requestid(), id);
        EXPECT_EQ(chained.result(), LocatorRegisterChainResponse::SUCCESS);

        for (bool present : {true, false})
        {
            SCOPED_TRACE(present);
            const std::string lookupKey = present ? key : key + "-missing";
            LocatorLookupRequest lookup;
            lookup.set_requestid(id);
            lookup.set_key(lookupKey);
            CapturingConnection lookupConn;
            ASSERT_TRUE(HandleLocatorLookup(lookupConn, lookup));
            LocatorLookupResponse found;
            ASSERT_TRUE(lookupConn.DecodeResponse(found));
            EXPECT_EQ(found.requestid(), id);
            EXPECT_EQ(found.result(), present ? LocatorLookupResponse::SUCCESS
                                             : LocatorLookupResponse::NOT_FOUND);

            LocatorPrefixLookupRequest prefix;
            prefix.set_requestid(id);
            prefix.add_keys(lookupKey);
            CapturingConnection prefixConn;
            ASSERT_TRUE(HandleLocatorPrefixLookup(prefixConn, prefix));
            LocatorPrefixLookupResponse matched;
            ASSERT_TRUE(prefixConn.DecodeResponse(matched));
            EXPECT_EQ(matched.requestid(), id);
            EXPECT_EQ(matched.matched_count(), present ? 1u : 0u);

            LocatorRefreshRequest refresh;
            refresh.set_requestid(id);
            refresh.set_key(lookupKey);
            refresh.set_nodeid(1);
            refresh.set_newexpiresat(EpochNowMs() + 120'000);
            CapturingConnection refreshConn;
            ASSERT_TRUE(HandleLocatorRefresh(refreshConn, refresh));
            LocatorRefreshResponse refreshed;
            ASSERT_TRUE(refreshConn.DecodeResponse(refreshed));
            EXPECT_EQ(refreshed.requestid(), id);
            EXPECT_EQ(refreshed.result(), present ? LocatorRefreshResponse::SUCCESS
                                                 : LocatorRefreshResponse::NOT_FOUND);

            LocatorUnregisterRequest unregister;
            unregister.set_requestid(id);
            unregister.set_key(lookupKey);
            unregister.set_nodeid(1);
            CapturingConnection unregisterConn;
            ASSERT_TRUE(HandleLocatorUnregister(unregisterConn, unregister));
            LocatorUnregisterResponse removed;
            ASSERT_TRUE(unregisterConn.DecodeResponse(removed));
            EXPECT_EQ(removed.requestid(), id);
            EXPECT_EQ(removed.result(), present ? LocatorUnregisterResponse::SUCCESS
                                               : LocatorUnregisterResponse::NOT_FOUND);
        }
    }
}

//---------------------------------------------------------------------------------------------------------------------
// Single registration forwards the new parentHash/rootHash fields into the locator entry.
TEST_F(LocatorRequestTest, RegisterForwardsParentAndRoot)
{
    const std::string strKey = "b:0000000000000101";
    const std::string strParent = "b:0000000000000100";
    const std::string strRoot = "b:0000000000000100";

    LocatorRegisterRequest request;
    request.set_key(strKey);
    request.set_requestid("req-reg-1");
    request.set_parenthash(strParent);
    request.set_roothash(strRoot);
    *request.mutable_replica() = MakeReplica(/*nNodeId=*/7, /*nContentSize=*/1024);

    CapturingConnection conn;
    ASSERT_TRUE(HandleLocatorRegister(conn, request));

    LocatorRegisterResponse response;
    ASSERT_TRUE(conn.DecodeResponse(response));
    EXPECT_EQ(response.result(), LocatorRegisterResponse::SUCCESS);

    auto entry = kvclient::GetServerLocatorCache().Lookup(strKey);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->m_strParentKey, strParent);
    EXPECT_EQ(entry->m_strRootKey, strRoot);
    ASSERT_EQ(entry->m_vReplicas.size(), 1u);
    EXPECT_EQ(entry->m_vReplicas[0].m_nNodeId, 7u);
    EXPECT_EQ(entry->m_vReplicas[0].m_nContentSize, 1024u);
}

//---------------------------------------------------------------------------------------------------------------------
// The batch chain-registration handler registers every block of the chain in a single RPC, and
// records each block's place in the prefix tree.
TEST_F(LocatorRequestTest, RegisterChainPopulatesEveryBlock)
{
    const std::string strRoot = "b:0000000000000200";
    const std::string strMid = "b:0000000000000201";
    const std::string strLeaf = "b:0000000000000202";

    LocatorRegisterChainRequest request;
    request.set_requestid("req-chain-1");

    struct Spec
    {
        std::string strKey;
        std::string strParent;
        std::string strRoot;
        uint32_t nNodeId;
        uint64_t nContentSize;
    };
    const std::vector<Spec> specs = {
        {strRoot, "", "", 1, 100},
        {strMid, strRoot, strRoot, 2, 200},
        {strLeaf, strMid, strRoot, 3, 300},
    };

    for (const auto& spec : specs)
    {
        auto* entry = request.add_entries();
        entry->set_key(spec.strKey);
        entry->set_parenthash(spec.strParent);
        entry->set_roothash(spec.strRoot);
        *entry->mutable_replica() = MakeReplica(spec.nNodeId, spec.nContentSize);
    }

    CapturingConnection conn;
    ASSERT_TRUE(HandleLocatorRegisterChain(conn, request));

    LocatorRegisterChainResponse response;
    ASSERT_TRUE(conn.DecodeResponse(response));
    EXPECT_EQ(response.result(), LocatorRegisterChainResponse::SUCCESS);

    for (const auto& spec : specs)
    {
        auto entry = kvclient::GetServerLocatorCache().Lookup(spec.strKey);
        ASSERT_TRUE(entry.has_value()) << "missing key " << spec.strKey;
        EXPECT_EQ(entry->m_strParentKey, spec.strParent);
        EXPECT_EQ(entry->m_strRootKey, spec.strRoot);
        ASSERT_EQ(entry->m_vReplicas.size(), 1u);
        EXPECT_EQ(entry->m_vReplicas[0].m_nNodeId, spec.nNodeId);
        EXPECT_EQ(entry->m_vReplicas[0].m_nContentSize, spec.nContentSize);
    }
}

//---------------------------------------------------------------------------------------------------------------------
// An empty chain is a well-formed no-op that still returns SUCCESS.
TEST_F(LocatorRequestTest, RegisterChainEmptyIsSuccess)
{
    LocatorRegisterChainRequest request;
    request.set_requestid("req-chain-empty");

    CapturingConnection conn;
    ASSERT_TRUE(HandleLocatorRegisterChain(conn, request));

    LocatorRegisterChainResponse response;
    ASSERT_TRUE(conn.DecodeResponse(response));
    EXPECT_EQ(response.result(), LocatorRegisterChainResponse::SUCCESS);
}

//---------------------------------------------------------------------------------------------------------------------
// Prefix-lookup handler tests. These run against a FlatRadixIndex backend, which supports the
// chain walk (the default flat backend does not).
//---------------------------------------------------------------------------------------------------------------------

// Register a full chain via the batch handler, then resolve it via the prefix-lookup handler:
// every block matches and the per-entry node_ids/content_sizes line up in chain order.
TEST_F(LocatorRequestTest, RegisterChainThenPrefixLookupReturnsWholeChain)
{
    UseRadixBackend();

    const std::string strRoot = "b:0000000000000300";
    const std::string strMid = "b:0000000000000301";
    const std::string strLeaf = "b:0000000000000302";

    LocatorRegisterChainRequest registerRequest;
    registerRequest.set_requestid("req-chain-2");
    struct Spec { std::string strKey, strParent, strRoot; uint32_t nNodeId; uint64_t nContentSize; };
    const std::vector<Spec> specs = {
        {strRoot, "", "", 1, 100},
        {strMid, strRoot, strRoot, 2, 200},
        {strLeaf, strMid, strRoot, 3, 300},
    };
    for (const auto& spec : specs)
    {
        auto* entry = registerRequest.add_entries();
        entry->set_key(spec.strKey);
        entry->set_parenthash(spec.strParent);
        entry->set_roothash(spec.strRoot);
        *entry->mutable_replica() = MakeReplica(spec.nNodeId, spec.nContentSize);
    }

    CapturingConnection registerConn;
    ASSERT_TRUE(HandleLocatorRegisterChain(registerConn, registerRequest));

    LocatorPrefixLookupRequest lookupRequest;
    lookupRequest.set_requestid("req-prefix-full");
    lookupRequest.add_keys(strRoot);
    lookupRequest.add_keys(strMid);
    lookupRequest.add_keys(strLeaf);
    // Chain roots at the caller, so the parent of the first block is empty.
    lookupRequest.set_baseparenthash("");

    CapturingConnection lookupConn;
    ASSERT_TRUE(HandleLocatorPrefixLookup(lookupConn, lookupRequest));

    LocatorPrefixLookupResponse response;
    ASSERT_TRUE(lookupConn.DecodeResponse(response));
    EXPECT_EQ(response.result(), LocatorPrefixLookupResponse::SUCCESS);
    ASSERT_EQ(response.matched_count(), 3u);
    ASSERT_EQ(response.matched_size(), 3);
    EXPECT_EQ(response.matched(0).node_replica().node_id(), 1u);
    EXPECT_EQ(response.matched(1).node_replica().node_id(), 2u);
    EXPECT_EQ(response.matched(2).node_replica().node_id(), 3u);
    EXPECT_EQ(response.matched(0).content_size(), 100u);
    EXPECT_EQ(response.matched(1).content_size(), 200u);
    EXPECT_EQ(response.matched(2).content_size(), 300u);
}

//---------------------------------------------------------------------------------------------------------------------
// Only the registered prefix matches; the walk stops at the first absent block.
TEST_F(LocatorRequestTest, PrefixLookupPartialMatchStopsAtGap)
{
    UseRadixBackend();

    const std::string strRoot = "b:0000000000000400";
    const std::string strMid = "b:0000000000000401";
    const std::string strLeaf = "b:0000000000000402"; // intentionally NOT registered

    RegisterBlock(strRoot, "", "", /*nNodeId=*/1, /*nContentSize=*/100);
    RegisterBlock(strMid, strRoot, strRoot, /*nNodeId=*/2, /*nContentSize=*/200);

    LocatorPrefixLookupRequest lookupRequest;
    lookupRequest.set_requestid("req-prefix-partial");
    lookupRequest.add_keys(strRoot);
    lookupRequest.add_keys(strMid);
    lookupRequest.add_keys(strLeaf);
    lookupRequest.set_baseparenthash("");

    CapturingConnection conn;
    ASSERT_TRUE(HandleLocatorPrefixLookup(conn, lookupRequest));

    LocatorPrefixLookupResponse response;
    ASSERT_TRUE(conn.DecodeResponse(response));
    EXPECT_EQ(response.result(), LocatorPrefixLookupResponse::SUCCESS);
    ASSERT_EQ(response.matched_count(), 2u);
    ASSERT_EQ(response.matched_size(), 2);
    EXPECT_EQ(response.matched(1).node_replica().node_id(), 2u);
    EXPECT_EQ(response.matched(1).content_size(), 200u);
}

//---------------------------------------------------------------------------------------------------------------------
// No block of the chain is registered: nothing matches.
TEST_F(LocatorRequestTest, PrefixLookupTotalMiss)
{
    UseRadixBackend();

    LocatorPrefixLookupRequest lookupRequest;
    lookupRequest.set_requestid("req-prefix-miss");
    lookupRequest.add_keys("b:0000000000000500");
    lookupRequest.add_keys("b:0000000000000501");
    lookupRequest.set_baseparenthash("");

    CapturingConnection conn;
    ASSERT_TRUE(HandleLocatorPrefixLookup(conn, lookupRequest));

    LocatorPrefixLookupResponse response;
    ASSERT_TRUE(conn.DecodeResponse(response));
    EXPECT_EQ(response.result(), LocatorPrefixLookupResponse::SUCCESS);
    EXPECT_EQ(response.matched_count(), 0u);
    EXPECT_EQ(response.matched_size(), 0);
}

//---------------------------------------------------------------------------------------------------------------------
// The base parent hash is honored: a chain whose first block descends from an unlisted root
// still matches when baseParentHash names that root.
TEST_F(LocatorRequestTest, PrefixLookupHonorsBaseParentHash)
{
    UseRadixBackend();

    const std::string strRoot = "b:0000000000000600"; // parent only; not part of the query keys
    const std::string strMid = "b:0000000000000601";
    const std::string strLeaf = "b:0000000000000602";

    RegisterBlock(strMid, strRoot, strRoot, /*nNodeId=*/2, /*nContentSize=*/200);
    RegisterBlock(strLeaf, strMid, strRoot, /*nNodeId=*/3, /*nContentSize=*/300);

    LocatorPrefixLookupRequest lookupRequest;
    lookupRequest.set_requestid("req-prefix-baseparent");
    lookupRequest.add_keys(strMid);
    lookupRequest.add_keys(strLeaf);
    lookupRequest.set_baseparenthash(strRoot);

    CapturingConnection conn;
    ASSERT_TRUE(HandleLocatorPrefixLookup(conn, lookupRequest));

    LocatorPrefixLookupResponse response;
    ASSERT_TRUE(conn.DecodeResponse(response));
    EXPECT_EQ(response.result(), LocatorPrefixLookupResponse::SUCCESS);
    EXPECT_EQ(response.matched_count(), 2u);

    // A wrong base parent breaks the chain at the very first block.
    LocatorPrefixLookupRequest wrongBase;
    wrongBase.set_requestid("req-prefix-wrongbase");
    wrongBase.add_keys(strMid);
    wrongBase.add_keys(strLeaf);
    wrongBase.set_baseparenthash("b:000000000000dead");

    CapturingConnection wrongConn;
    ASSERT_TRUE(HandleLocatorPrefixLookup(wrongConn, wrongBase));

    LocatorPrefixLookupResponse wrongResponse;
    ASSERT_TRUE(wrongConn.DecodeResponse(wrongResponse));
    EXPECT_EQ(wrongResponse.matched_count(), 0u);
}

//---------------------------------------------------------------------------------------------------------------------
// Per matched entry, the response reports the best non-expired replica (skipping expired ones).
TEST_F(LocatorRequestTest, PrefixLookupReportsBestNonExpiredReplica)
{
    UseRadixBackend();

    const std::string strKey = "b:0000000000000700";

    // A recently expired UNIX timestamp is still much larger than a typical steady-clock value,
    // so this specifically verifies that the handler compares expiry using the UNIX epoch.
    RegisterBlock(strKey, "", "", /*nNodeId=*/9, /*nContentSize=*/111,
                  /*nExpiresAt=*/EpochNowMs() - 60'000);
    RegisterBlock(strKey, "", "", /*nNodeId=*/8, /*nContentSize=*/222);

    LocatorPrefixLookupRequest lookupRequest;
    lookupRequest.set_requestid("req-prefix-replica");
    lookupRequest.add_keys(strKey);
    lookupRequest.set_baseparenthash("");

    CapturingConnection conn;
    ASSERT_TRUE(HandleLocatorPrefixLookup(conn, lookupRequest));

    LocatorPrefixLookupResponse response;
    ASSERT_TRUE(conn.DecodeResponse(response));
    EXPECT_EQ(response.result(), LocatorPrefixLookupResponse::SUCCESS);
    ASSERT_EQ(response.matched_count(), 1u);
    ASSERT_EQ(response.matched_size(), 1);
    EXPECT_EQ(response.matched(0).node_replica().node_id(), 8u);
    EXPECT_EQ(response.matched(0).content_size(), 222u);
}

//---------------------------------------------------------------------------------------------------------------------
// Mid-chain resolution: with a full chain A-B-C-D-E registered, a lookup rooted at an interior
// block (baseParent = C) matches the requested suffix D, E.
TEST_F(LocatorRequestTest, PrefixLookupMidChainMatchesSuffix)
{
    UseRadixBackend();

    const std::string strA = "b:0000000000000800";
    const std::string strB = "b:0000000000000801";
    const std::string strC = "b:0000000000000802";
    const std::string strD = "b:0000000000000803";
    const std::string strE = "b:0000000000000804";

    RegisterBlock(strA, "", "", /*nNodeId=*/1, /*nContentSize=*/100);
    RegisterBlock(strB, strA, strA, /*nNodeId=*/2, /*nContentSize=*/200);
    RegisterBlock(strC, strB, strA, /*nNodeId=*/3, /*nContentSize=*/300);
    RegisterBlock(strD, strC, strA, /*nNodeId=*/4, /*nContentSize=*/400);
    RegisterBlock(strE, strD, strA, /*nNodeId=*/5, /*nContentSize=*/500);

    LocatorPrefixLookupRequest lookupRequest;
    lookupRequest.set_requestid("req-prefix-midchain");
    lookupRequest.add_keys(strD);
    lookupRequest.add_keys(strE);
    lookupRequest.set_baseparenthash(strC);

    CapturingConnection conn;
    ASSERT_TRUE(HandleLocatorPrefixLookup(conn, lookupRequest));

    LocatorPrefixLookupResponse response;
    ASSERT_TRUE(conn.DecodeResponse(response));
    EXPECT_EQ(response.result(), LocatorPrefixLookupResponse::SUCCESS);
    ASSERT_EQ(response.matched_count(), 2u);
    ASSERT_EQ(response.matched_size(), 2);
    EXPECT_EQ(response.matched(0).node_replica().node_id(), 4u);
    EXPECT_EQ(response.matched(1).node_replica().node_id(), 5u);
    EXPECT_EQ(response.matched(0).content_size(), 400u);
    EXPECT_EQ(response.matched(1).content_size(), 500u);
}

//---------------------------------------------------------------------------------------------------------------------
// Mid-chain resolution stops at the first unregistered block: with A-B-C-D-E registered, a lookup
// (baseParent = C) for [D, F] matches only D because F is not part of the chain.
TEST_F(LocatorRequestTest, PrefixLookupMidChainStopsAtUnregisteredBlock)
{
    UseRadixBackend();

    const std::string strA = "b:0000000000000810";
    const std::string strB = "b:0000000000000811";
    const std::string strC = "b:0000000000000812";
    const std::string strD = "b:0000000000000813";
    const std::string strE = "b:0000000000000814";
    const std::string strF = "b:0000000000000815"; // intentionally NOT registered

    RegisterBlock(strA, "", "", /*nNodeId=*/1, /*nContentSize=*/100);
    RegisterBlock(strB, strA, strA, /*nNodeId=*/2, /*nContentSize=*/200);
    RegisterBlock(strC, strB, strA, /*nNodeId=*/3, /*nContentSize=*/300);
    RegisterBlock(strD, strC, strA, /*nNodeId=*/4, /*nContentSize=*/400);
    RegisterBlock(strE, strD, strA, /*nNodeId=*/5, /*nContentSize=*/500);

    LocatorPrefixLookupRequest lookupRequest;
    lookupRequest.set_requestid("req-prefix-midchain-gap");
    lookupRequest.add_keys(strD);
    lookupRequest.add_keys(strF);
    lookupRequest.set_baseparenthash(strC);

    CapturingConnection conn;
    ASSERT_TRUE(HandleLocatorPrefixLookup(conn, lookupRequest));

    LocatorPrefixLookupResponse response;
    ASSERT_TRUE(conn.DecodeResponse(response));
    EXPECT_EQ(response.result(), LocatorPrefixLookupResponse::SUCCESS);
    ASSERT_EQ(response.matched_count(), 1u);
    ASSERT_EQ(response.matched_size(), 1);
    EXPECT_EQ(response.matched(0).node_replica().node_id(), 4u);
    EXPECT_EQ(response.matched(0).content_size(), 400u);
}
