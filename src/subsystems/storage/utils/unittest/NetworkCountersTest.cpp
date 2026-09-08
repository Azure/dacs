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
//
//---------------------------------------------------------------------------------------------------------------------
#include "NetworkCountersTest.h"
//---------------------------------------------------------------------------------------------------------------------
TEST_F(NetworkCountersTest, ReadNetworkCounters)
{
    NetworkCounters counters;
    counters.listenOverflows = std::numeric_limits<uint64_t>::max();
    counters.listenDrops = std::numeric_limits<uint64_t>::max();
    counters.synQueueFullDrops = std::numeric_limits<uint64_t>::max();
    counters.synCookiesSent = std::numeric_limits<uint64_t>::max();
    counters.syncookiesRecv = std::numeric_limits<uint64_t>::max();
    counters.passiveOpens = std::numeric_limits<uint64_t>::max();
    counters.activeOpens = std::numeric_limits<uint64_t>::max();
    counters.openFdCount = std::numeric_limits<uint64_t>::max();

    const bool procNetstatRead = ReadNetstatCounters(counters);
    const bool tcpOpensRead = ReadTcpOpenCounters(counters.passiveOpens, counters.activeOpens);
    const bool openFdRead = ReadOpenFdCount(counters.openFdCount);

    EXPECT_TRUE(procNetstatRead);
    EXPECT_NE(counters.listenOverflows, std::numeric_limits<uint64_t>::max());
    EXPECT_NE(counters.listenDrops, std::numeric_limits<uint64_t>::max());
    EXPECT_NE(counters.synQueueFullDrops, std::numeric_limits<uint64_t>::max());
    EXPECT_NE(counters.synCookiesSent, std::numeric_limits<uint64_t>::max());
    EXPECT_NE(counters.syncookiesRecv, std::numeric_limits<uint64_t>::max());

    EXPECT_TRUE(tcpOpensRead);
    EXPECT_NE(counters.passiveOpens, std::numeric_limits<uint64_t>::max());
    EXPECT_NE(counters.activeOpens, std::numeric_limits<uint64_t>::max());

    EXPECT_TRUE(openFdRead);
    EXPECT_NE(counters.openFdCount, std::numeric_limits<uint64_t>::max());
}
//---------------------------------------------------------------------------------------------------------------------